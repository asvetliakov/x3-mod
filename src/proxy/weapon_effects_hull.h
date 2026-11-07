#pragma once
#include "weapon_effects_stage.h"
#include "engine_shimmer_core.h" // engine_shimmer::WeaponRequest (the shimmer rects' hand-off)
#include <xmmintrin.h>

// The hull hit's scene-end step (docs/architecture/weapon-look.md 2.4-2.11 and 3.1, step 2; the per-class anatomy of
// docs/architecture/hull-impact-classes.md): the ring's stage-owned hull hits (role_hull_hit with flag_stage,
// shields,hull mode) become the frame's flash, streak and spark quads (the sprite batch, renderer::WeaponSpriteVertex),
// glow and ember boxes (the glow primitive, renderer::WeaponGlowVertex), puffs (the fireball impostor,
// renderer::WeaponFireballVertex), surface rings (renderer::WeaponShapeVertex), the ion's lightning segments
// (renderer::WeaponLightningVertex), multiply scorches (glow boxes drawn ZERO / INVSRCCOLOR) and heat-shimmer rects
// (ShimmerRect, the engine shimmer pass's list).
//
// Per record, at the contact point Event::pos in its owner's frame, sized in the owner's cell unit U = cell_m[tier]
// and the impact's calibre k (calibre_of: the running damage total, or the override's): r = r_hit = r_scale U (r_base +
// r_k k). Its anatomy (anatomy_of: the asteroid victim, then repair, tug, a missile source, the class nibble of
// Event::detail the hook wrote) selects one row of each core::hull::class_* table:
// - flash (docs/architecture/hull-burst-look.md section 4): a four-arm star of unequal arms at a hashed angle, a white
//   core, no halo, linear over its 40 ms (three frames at least), biased towards the eye by bias r; the heavy class adds
//   the entry streak along the shot (P - streak_back r a to P + streak_front r a; without Event::dir the star's arms at
//   streak_fallback_halo r instead);
// - burst (section 2): the fireball impostor riding the owner, R(t) = R_b (1 - e^(-t / (rise L))) (the plasma globe
//   shrinking over the last 0.7 L), heat T0 -> 0, the eroded three-octave noise body in its lobed silhouette (the third
//   octave weighted in over look::fireball_fine_px .. + fireball_fine_band_px), the tint mixed in for the plasma globe, the asteroid's unlit dust; the lane clips its
//   chord; scaled per owner cluster (section 3); its smoke (section 5) the same impostor in the multiply boxes from
//   smoke_t0 L to (smoke_t0 + smoke_span) L;
// - spark sets (fast, slow, chips): per hit round(n (1 +- spark_count_spread)) about n = n0 + nk k, in antipodal pairs
//   of hashed isotropic directions u, each launched along normalize(+-u + b a) (a the shot heading in world, b the set's
//   bias: b < 0 back towards the shooter, b > 0 through the hull; b 0 without Event::dir), each spark's speed, life,
//   rest length and width x its own hashed factors, a quarter curling, plus the owner's velocity at the first stage
//   frame, closed form p(t) = p0 + v0 tau (1 - e^(-t / tau)) + w t in world (they leave the owner and outlive it); drawn
//   from the previous frame's screen position to the current one (the streak rule); radiance E f, heat T0 f with f the
//   life fraction (the ion's in ion_colour); a set with a duty is lit in that share of flicker_ms slots;
// - glow (the burst's light: glow_r_x R(t), its heat and life, the cluster's saturation; repair's and tug's cool spot) and
//   ember (the glow primitive at the hit), the surface ring (the sphere shell cut by the lane), the ion's lightning
//   (ion_phase: a strike every ion_period_lo_ms..ion_period_hi_ms re-seeding its channels to ion_strike_until of
//   ion_life_ms, each flash lingering as a faint afterglow for one period, the last 1 - ion_fade_from of the life fading;
//   ion_strike: 3 to 6 jagged channels with 1 to 3 branches each in the hit's tangent plane riding the owner, one
//   segment quad each drawn on the lane under it; the cold light on the hull, the glow primitive in ion_colour
//   flickering with the strikes) and the plasma / heavy shimmer rect;
// - the scorch: at a record's first frame a mark enters the per-owner scorch store (scorch_per_owner, scorch_store; it
//   outlives the record), drawn every frame as a multiply decal s0 min(1, t / L_ember) (1 - t / L_s) until L_s.
// Beams, repair and tug follow the hold rule (section 3.9): a new record of an owner and (table, row) within hold_r r of
// a live spot of theirs re-lights it (its newest tick) and leaves the ring; otherwise it becomes a new spot, flashing
// only when the owner held no spot of that (table, row). A spot is held while now - t_last <= max(hold_ms, 2 steps),
// then falls; the beam's spot draws a core, a boiling puff, spatter (closed form: two sparks per spatter_period_ms of
// its hold), a glow and a trail ember from its fall, and lays a scorch mark every trail_step_r r it crawls (the spot's
// halo, core and glow in the weapon colour mixed with white, beam_glow_mix / beam_core_mix; the pulse star, puff, smoke,
// spatter, ember and scorch heat); repair draws a cool star at its pulse start, a cool glow and ember, tug a fainter
// cool glow.
// Every life is game time from the record's first stage frame (age 0 on its first drawn frame) and never shorter than
// min_frames x its first frame's game step (the three-frame floor), each element's life x (1 +- stagger / 2) by a hash
// of its record and index. The owner-attached elements ride the owner (re-posed every frame through its rows, validated
// as the shield stage validates); a record whose owner went after its first frame keeps its sparks only (orphan), one
// whose owner never validated leaves the ring (retired); a spot or scorch whose owner went is freed. The own ship in
// the cockpit view: every element of a hit on it at own_cockpit_gain (look 2.9).
// The shared budget (look 2.11 as amended by hull-impact-classes.md section 6): each box is charged its screen rectangle
// x its family weight; past the screens the patches left (hull-burst-look.md section 6): (0) the shimmer rects of other
// owners, (1) every burst's third octave, (2) other owners' smoke, oldest first, (3) their scorches, (4) their rings and
// lightning segments, (5) their glows and embers, oldest first, then their bursts past two per owner, last their
// flashes. Entry
// streaks, sparks, chips, beam spot cores and anything of the own ship are never shed (count caps: hull::max_sparks;
// renderer::weapon_sprite_quads, weapon_glow_boxes, weapon_fireball_boxes, weapon_ring_quads, weapon_filament_boxes,
// weapon_scorch_boxes, weapon_smoke_boxes; hull::max_spots, scorch_store, shimmer_max).
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
    // The frame floor (ms) the cached reach below was computed with; negative: not computed yet. In the padding before
    // p0 (the slot stays 512 bytes).
    float reach_floor_ms = -1.f;
    double p0[3]{};   // the contact point in world (view units) at the record's first stage frame
    float v[3]{};     // the owner's velocity then (world, view units per game second)
    float axis[3]{};  // the shot heading in world (Event::dir through the owner's rows then), unit; has_axis
    float floor_ms = 0.f; // the three-frame floor latched at the first drawn frame (min_frames x that frame's step)
    // The record's element reach (view units; record_reach at its first stage frame, again at the latch when floor_ms
    // differs from that frame's floor): from the contact point (flash, streak, puff, ring, filament, shimmer, glow,
    // ember) and from the spark origin (sparks and chips)
    float reach_attached = 0.f, reach_spark = 0.f;
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
    // The budget's octave step (1) with its hysteresis (look::fireball_octave_hold, fireball_octave_return): whether the
    // bursts draw two octaves and the frames since it began.
    bool octaves_coarse = false;
    std::uint8_t coarse_frames = 0;
    // The budget's tongue step (0c) with the same hysteresis (fire-material.md section 7).
    bool fire_coarse = false;
    std::uint8_t fire_frames = 0;
    // The tongues' own fence (hull::fire_fence; fire-material.md section 12) with the same hysteresis.
    bool fire_fenced = false;
    std::uint8_t fence_frames = 0;
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
    s.octaves_coarse = false;
    s.coarse_frames = 0;
    s.fire_coarse = false;
    s.fire_frames = 0;
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
    bool beam_colour = core::hull::beam_colour; // the beam spot's halo, core and glow in the weapon colour
    bool ring_noise = true, ring_wash = true, ring_refraction = true; // the shock front's parts (the lab's sub-layers)
    bool smoke = true;     // the bursts' smoke (hull-burst-look.md section 5)
    bool cluster = true;   // the per-owner cluster saturation of the bursts and their glows (section 3)
    bool spot_core = true; // a beam spot's held core (the fixture isolates the pulse's star)
    bool glow_to_ring = true; // a ring class's glow out to the ring (the fixture's before image: false)
    bool cluster_fade = true; // a neighbour counted by its remaining life fraction (the fixture's hard count: false)
    unsigned cluster_grid_min = 128; // an owner's bursts from which the cluster count takes the grid (the fixture times both)
    float fine_band_px = core::look::fireball_fine_band_px; // the third octave's blend (the fixture's hard step: 0)
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
    float stagger = core::stagger;
    float own_gain = core::own_cockpit_gain;
    unsigned min_frames = core::look::min_frames;
    float spark_count_x = core::hull::spark_count_x; // the kill criterion's halving (a header decision, never a setting)
    unsigned set_mask = 7; // the spark sets drawn (bit 0 fast, 1 slow, 2 chips; the fixture isolates one)
    bool reach_cull = true; // a kept owner out of reach builds no elements (the fixture's pre-change image: false)
    bool fire_core = true;  // the fire classes' white-hot core (fire-material.md 2.3; the fixture's cores-off frame: false)
    bool fire = true;       // the fire material (false: the previous law; the fixture's octave-step rows)
};
struct HullCounts {
    unsigned hits = 0, flashes = 0, sparks = 0, glows = 0, embers = 0, bursts = 0, rings = 0;
    unsigned orphans = 0, retired = 0, shed = 0, capped = 0, floored = 0, owners_dropped = 0;
    unsigned bursts_shed = 0;
    unsigned streaks = 0, chips = 0, filaments = 0, scorches = 0, marks = 0, spots = 0, merges = 0, spot_flashes = 0;
    unsigned ion_segments = 0; // the lightning's segment quads (filaments: the ion records whose strike is lit; for the
                               // lightning, capped counts the hits cut whole at the segment cap)
    unsigned pulses = 0; // spots whose pulse started this frame (a new spot, or a re-lit one after a gap)
    unsigned done = 0, flashes_shed = 0; // records released at their anatomy's end; flashes the budget shed (last)
    unsigned out_of_reach = 0, owners_out_of_reach = 0; // records (and kept owners) that built no elements: out of reach
    unsigned shimmer = 0, shimmer_shed = 0, scorches_shed = 0, rings_shed = 0, filaments_shed = 0, dir_fallback = 0;
    unsigned anatomy[core::hull::anatomy_count]{}; // live records (and spots) per anatomy this frame
    unsigned octaves = core::look::fireball_octaves; // the bursts' noise octaves at most this frame (2: the budget's step 1)
    unsigned fire_shed = 0; // the bursts whose tongues the budget's step (0c) took this frame (fire-material.md 7)
    unsigned fine = 0;   // bursts drawn without their full third octave by their size (under fine_px + fine_band_px)
    unsigned smokes = 0, smokes_shed = 0; // the bursts' smoke boxes (the multiply pass) and the budget's shed of them
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
    renderer::WeaponLightningVertex* filaments = nullptr; // the ion's lightning segments
    unsigned filament_cap = 0, filament_boxes = 0;
    renderer::WeaponGlowVertex* scorches = nullptr;
    unsigned scorch_cap = 0, scorch_boxes = 0;
    renderer::WeaponFireballVertex* smokes = nullptr; // nullptr: no smoke (no multiply blend)
    unsigned smoke_cap = 0, smoke_boxes = 0;
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
// dust puff and rock spatter. A beam is the beam class or any record whose bullet carries the beam bits (its style's
// shape_beam, core::style_from), whatever its class: stock IOND, pinned ion, is a beam (2026-11-07: one 400 ms
// lightning hit per damage tick before).
inline bool holds(const core::Event& e) noexcept {
    return (e.detail & (core::detail_repair | core::detail_tug)) || core::hull_class_of(e) == core::class_beam ||
           (e.style & core::style_shape_mask) == core::shape_beam;
}
// A holding record the spots draw (the beam's, the asteroid's dust, repair, tug); any other anatomy (a pinned class
// with the beam bits: the ion's lightning) is held in the ring itself, its newest tick refreshing the record in place.
inline bool spot_anatomy(unsigned a) noexcept {
    namespace hl = core::hull;
    return a == hl::an_beam || a == hl::an_asteroid || a == hl::an_repair || a == hl::an_tug;
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
    // step 3's pixel mode (b.w < 0, the death sparks): -b.w the head's FWHM and |c.x| the tail's length, pixels, no floors
    const bool px_mode = v[0].b[3] < 0.f;
    const float len_x = v[0].c[0] < 0.f ? -v[0].c[0] : v[0].c[0];
    float lit = px_mode ? -v[0].b[3] : v[0].b[3] * sc.hw * f.cam.m00 / v[0].a[2];
    o.lit = px_mode || lit > core::look::floor_lit_px ? lit : core::look::floor_lit_px;
    const float rest_w = core::hull::spark_rest_w * o.lit;
    o.rest = px_mode ? len_x : (rest_w > core::look::floor_len_px ? rest_w : core::look::floor_len_px) * len_x;
    const float cap = core::look::streak_cap * o.rest;
    const float drawn = px_mode ? o.rest : o.motion < o.rest ? o.rest : o.motion > cap ? cap : o.motion;
    // the pixel mode's quad: the head's halo window (2 x its FWHM + 0.5 px) about the head and the tail
    const float hw = px_mode ? 2.f * o.lit + 0.5f : 0.5f * o.lit, nx = -dy * hw, ny = dx * hw;
    for (unsigned c = 0; c < 4; ++c) {
        const bool at_head = v[c].c[0] > 0.f;
        const float along = at_head ? hw : -(drawn + hw);
        o.x[c] = head.x + dx * along + nx * v[c].c[1];
        o.y[c] = head.y + dy * along + ny * v[c].c[1];
        o.z[c] = at_head ? v[c].a[2] : v[c].b[2];
    }
    const float r = px_mode ? v[0].a[3] : v[0].a[3] * o.rest / (o.rest + o.motion);
    o.radiance = px_mode || r > v[0].c[2] ? r : v[0].c[2];
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
                      renderer::WeaponGlowVertex* out, const float* lobe = nullptr) noexcept {
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
        out[k].lobe[0] = lobe ? lobe[0] : 0.f;
        out[k].lobe[1] = lobe ? lobe[1] : -1.f;
    }
}
// A fireball's four corners (the glow's rectangle, weapon_fireball_vs.hlsl): at view depth z_q (the sphere's front, or
// 1.5 x the near plane for the whole screen), each corner carrying the sphere, its look (T, E, age fraction, seed), its
// tint (rgb, share; share < 0 smoke) and its disc (rotation, lobe seed, advection, the third octave's weight); core: the
// white-hot core's gain, the fire weight, the size LOD and the game-time age in ms (nullptr: zeros, the previous law).
inline void fireball_quad(const HullView& f, const float c[3], float R, const float rect[4], bool full, const float look[4],
                          const float tint[4], const float disc[4], renderer::WeaponFireballVertex* out,
                          const float* core = nullptr) noexcept {
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
        for (unsigned i = 0; i < 4; ++i) out[k].disc[i] = disc[i];
        for (unsigned i = 0; i < 4; ++i) out[k].core[i] = core ? core[i] : 0.f;
    }
}
// sin and cos of a in [0, pi / 2] (the star's angle): Taylor terms about pi / 4 (error below 1e-8), no libm, no x87.
inline void sincos_quarter(float a, float* s, float* c) noexcept {
    const float d = a - 0.78539816f, d2 = d * d;
    const float sd = d * (1.f - d2 / 6.f * (1.f - d2 / 20.f * (1.f - d2 / 42.f * (1.f - d2 / 72.f))));
    const float cd = 1.f - d2 / 2.f * (1.f - d2 / 12.f * (1.f - d2 / 30.f * (1.f - d2 / 56.f)));
    *c = 0.70710678f * (cd - sd);
    *s = 0.70710678f * (sd + cd);
}
// A star's quad (the sprite batch's star kind, the hull hit's flash: hull-burst-look.md section 4): its centre c (view)
// biased towards the eye by bias r_hit; the arm unit A = arm_r r_hit (>= star_floor_px), the quad's half A + half the
// width floor, rotated by the angle on screen so (u, v) run along the arms; the core core_frac r_hit (>=
// floor_flash_px); the arms' colour, the radiance. False when behind the near plane.
inline bool star_quad(const HullView& f, const Screen& sc, const float c_view[3], float r_hit, float core_frac, float arm_r,
                      float angle, const float colour[3], float radiance, renderer::WeaponSpriteVertex* v,
                      float* screens) noexcept {
    namespace hl = core::hull;
    const float px_x = 0.5f * f.width * f.cam.m00;
    const float dist = scalar::sqrt(dot3(c_view, c_view));
    const float b = dist > 1e-6f ? 1.f - hl::bias * r_hit / dist : 1.f;
    const float c[3] = {c_view[0] * b, c_view[1] * b, c_view[2] * b};
    if (!(c[2] > f.near_z)) return false;
    const Px p = to_px(f, sc, c);
    const float r_px = r_hit * px_x * p.iz;
    float A_px = arm_r * r_px;
    if (A_px < hl::star_floor_px) A_px = hl::star_floor_px;
    float core_px = core_frac * r_px;
    if (core_px < 0.5f * core::look::floor_flash_px) core_px = 0.5f * core::look::floor_flash_px;
    const float fw_px = 0.5f * hl::star_width_px, half = A_px + fw_px;
    if (core_px > half) core_px = half;
    const float r_view = half * c[2] / px_x;
    const std::uint32_t packed = pack_colour(colour, core_px / half);
    float sn, cs;
    sincos_quarter(angle, &sn, &cs);
    // its charge: the rotated square's bounding box clipped to the screen, x the square's share of the box
    const float e = half * (cs + sn);
    const float x0 = p.x - e > 0.f ? p.x - e : 0.f, x1 = p.x + e < f.width ? p.x + e : f.width;
    const float y0 = p.y - e > 0.f ? p.y - e : 0.f, y1 = p.y + e < f.height ? p.y + e : f.height;
    *screens = x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) * (half * half) / (e * e * f.width * f.height) : 0.f;
    const float sx[4] = {-1.f, 1.f, -1.f, 1.f}, sy[4] = {1.f, 1.f, -1.f, -1.f};
    for (unsigned q = 0; q < 4; ++q) {
        const float du = half * (sx[q] * cs - sy[q] * sn), dv = half * (sx[q] * sn + sy[q] * cs);
        px_clip(f, sc, p.x + du, p.y - dv, c[2], v[q].a);
        v[q].b[0] = sx[q];
        v[q].b[1] = sy[q];
        v[q].b[2] = radiance;
        v[q].b[3] = r_view;
        v[q].c[0] = 3.f;
        v[q].c[1] = A_px / half;
        v[q].c[2] = fw_px / half;
        v[q].colour = packed;
    }
    return true;
}
// The stretched ring's box (the shock front, 2026-11-07): the ellipsoid |M d| <= R0, s times longer along the unit
// heading a, lies inside the capsule of radius R0 about the segment c +- (s - 1) R0 a (its distance from the segment is
// at most R0 at both ends of the stretched axis and in between), whose projection lies inside the hull of its end
// spheres' (perspective keeps convexity in front of the eye): the union of their rects. Full when the whole reach
// s R0 crosses the near plane (as sphere_rect). False when both ends lie off screen.
inline bool capsule_rect(const HullView& f, const float c[3], const float a[3], float stretch, float R0, float r[4],
                         bool* full) noexcept {
    const float e = (stretch - 1.f) * R0;
    if (!(e > 0.f)) return sphere_rect(f, c, R0, r, full);
    *full = !(c[2] - stretch * R0 > f.near_z);
    if (*full) {
        r[0] = r[1] = -1.f;
        r[2] = r[3] = 1.f;
        return true;
    }
    const float p0[3] = {c[0] + e * a[0], c[1] + e * a[1], c[2] + e * a[2]};
    const float p1[3] = {c[0] - e * a[0], c[1] - e * a[1], c[2] - e * a[2]};
    float r0[4], r1[4];
    bool f0 = false, f1 = false;
    const bool h0 = sphere_rect(f, p0, R0, r0, &f0), h1 = sphere_rect(f, p1, R0, r1, &f1);
    if (!h0 && !h1) return false;
    for (unsigned k = 0; k < 4; ++k) {
        const float u = h0 ? r0[k] : r1[k], v = h1 ? r1[k] : r0[k];
        r[k] = k < 2 ? (u < v ? u : v) : (u > v ? u : v);
    }
    return true;
}
// A shaped glow's four corners (weapon_shape_vs.hlsl): the glow's rectangle with the profile's terms (ring: r(t), w;
// the ring's frame; the lightning's segments use ion_segment_quad).
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
// The ion's lightning (core::hull ion_*; hull-impact-classes.md 4.6). One strike's channels and their side branches as
// polylines in the hit's tangent plane (u, v in r_hit) from the record's sequence and the strike index; no libm (the
// turns by 2 atan(t) in rational form). The lab's ionStrike is the same law.
struct IonLine {
    std::uint8_t first, points, branch; // its points in IonStrike::p; branch 1: a side branch
};
struct IonStrike {
    static constexpr unsigned max_lines = core::hull::ion_channels_hi * (1 + core::hull::ion_branches_hi);
    static constexpr unsigned branch_segments_hi = core::hull::ion_segments_hi / 2 > 3 ? core::hull::ion_segments_hi / 2 : 3;
    static constexpr unsigned max_points =
        core::hull::ion_channels_hi * (core::hull::ion_segments_hi + 1 + core::hull::ion_branches_hi * (branch_segments_hi + 1));
    unsigned lines = 0, points = 0, channels = 0, branches = 0;
    float extent = 0.f; // its farthest point from the hit (r_hit units; the cold light's radius, ion_glow_r x it)
    IonLine line[max_lines];
    float p[max_points][2];
    float len[max_points]; // the segment from point i to i + 1 (r_hit units; a line's last point: 0)
};
static_assert(IonStrike::max_points <= 255, "the lines' point indices");
inline unsigned ion_channel_count(std::uint32_t sequence) noexcept {
    namespace hl = core::hull;
    return hl::ion_channels_lo + (element_hash(sequence, 40) >> 8) % (hl::ion_channels_hi - hl::ion_channels_lo + 1);
}
// Strike j's period (ms): hashed in ion_period_lo_ms..ion_period_hi_ms, at least the record's three-frame floor.
inline float ion_period(std::uint32_t sequence, unsigned j, float floor_ms) noexcept {
    namespace hl = core::hull;
    const float p = float(hl::ion_period_lo_ms +
                          (element_hash(sequence, 700u + j) >> 8) % (hl::ion_period_hi_ms - hl::ion_period_lo_ms + 1));
    return p > floor_ms ? p : floor_ms;
}
// The strikes of a life: those starting before ion_strike_span_ms (at most ion_strikes_max).
inline unsigned ion_strike_count(std::uint32_t sequence, float floor_ms) noexcept {
    namespace hl = core::hull;
    float t = 0.f;
    unsigned n = 1;
    while (n < hl::ion_strikes_max && (t += ion_period(sequence, n - 1, floor_ms)) < float(hl::ion_strike_span_ms)) ++n;
    return n;
}
// (x, y) turned by the angle 2 atan(t).
inline void ion_turn(float t, float d[2]) noexcept {
    const float s = rcp_nr(1.f + t * t), c = (1.f - t * t) * s, n = 2.f * t * s;
    const float x = d[0];
    d[0] = c * x - n * d[1];
    d[1] = n * x + c * d[1];
}
// One polyline from o along d (unit), length L, n segments: each vertex steps L / n along d and up to ion_jag x that
// sideways (hashed from h), the sideways offset clamped to ion_lateral L.
inline void ion_walk(IonStrike* s, const float o[2], const float d[2], float L, unsigned n, std::uint32_t h,
                     std::uint8_t branch) noexcept {
    namespace hl = core::hull;
    if (s->lines >= IonStrike::max_lines || s->points + n + 1 > IonStrike::max_points) return;
    IonLine& ln = s->line[s->lines++];
    ln.first = std::uint8_t(s->points);
    ln.points = std::uint8_t(n + 1);
    ln.branch = branch;
    const float step = L / float(n), lim = hl::ion_lateral * L;
    float y = 0.f;
    for (unsigned i = 0; i <= n; ++i) {
        if (i) {
            y += step * hl::ion_jag * (2.f * unit_of(mix32(h + 0x85ebca6bu * i)) - 1.f);
            y = y > lim ? lim : y < -lim ? -lim : y;
        }
        const float x = step * float(i);
        float* p = s->p[s->points];
        p[0] = o[0] + d[0] * x - d[1] * y;
        p[1] = o[1] + d[1] * x + d[0] * y;
        s->len[s->points] = 0.f;
        if (i) {
            const float* q = s->p[s->points - 1];
            const float dx = p[0] - q[0], dy = p[1] - q[1];
            s->len[s->points - 1] = scalar::sqrt(dx * dx + dy * dy);
        }
        ++s->points;
    }
}
// The channel directions at c x 360 / count degrees (count 2, 3 or 4): cos and sin of the multiples of 30 degrees.
constexpr float ion_cos30[12] = {1.f, 0.8660254f, 0.5f, 0.f, -0.5f, -0.8660254f, -1.f, -0.8660254f, -0.5f, 0.f, 0.5f, 0.8660254f};
inline void ion_strike(std::uint32_t sequence, unsigned strike, IonStrike* s) noexcept {
    namespace hl = core::hull;
    *s = IonStrike{};
    const unsigned count = ion_channel_count(sequence);
    s->channels = count;
    const std::uint32_t h0 = element_hash(sequence, 600u + strike);
    const std::uint32_t hb = mix32(h0 ^ 0x5bd1e995u);
    const float tb = 2.f * unit_of(hb) - 1.f, ib = rcp_nr(1.f + tb * tb), sg = (hb & 1u) ? -1.f : 1.f;
    const float base[2] = {sg * (1.f - tb * tb) * ib, sg * 2.f * tb * ib};
    for (unsigned c = 0; c < count; ++c) {
        const std::uint32_t hc = mix32(h0 + 0x9e3779b9u * (c + 1));
        const unsigned k = (c * 12u / count) % 12u;
        const float cs = ion_cos30[k], sn = ion_cos30[(k + 9u) % 12u]; // sin(a) = cos(a - 90)
        float d[2] = {cs * base[0] - sn * base[1], sn * base[0] + cs * base[1]};
        ion_turn(hl::ion_dir_jitter * (2.f * unit_of(hc) - 1.f), d);
        const float L = hl::ion_len * (1.f + hl::ion_len_spread * (unit_of(mix32(hc ^ 0x27d4eb2fu)) - 0.5f));
        const unsigned n = hl::ion_segments_lo + (mix32(hc ^ 0x165667b1u) >> 8) % (hl::ion_segments_hi - hl::ion_segments_lo + 1);
        const float o[2] = {0.f, 0.f};
        const unsigned first = s->points;
        ion_walk(s, o, d, L, n, hc, 0);
        const unsigned nb = hl::ion_branches_lo + (mix32(hc ^ 0xd3a2646cu) >> 8) % (hl::ion_branches_hi - hl::ion_branches_lo + 1);
        for (unsigned b = 0; b < nb; ++b) {
            const std::uint32_t hr = mix32(hc + 0xc2b2ae35u * (b + 1));
            const unsigned j = 1u + (hr >> 8) % (n - 2u); // an inner vertex
            float t = hl::ion_branch_t_lo + (hl::ion_branch_t_hi - hl::ion_branch_t_lo) * unit_of(mix32(hr + 1u));
            if (hr & 1u) t = -t;
            float db[2] = {d[0], d[1]};
            ion_turn(t, db);
            const unsigned nbs = n / 2 > 3 ? n / 2 : 3;
            const unsigned lines = s->lines;
            ion_walk(s, s->p[first + j], db, hl::ion_branch_len * L, nbs, hr, 1);
            s->branches += s->lines > lines;
        }
    }
    float r2 = 0.f;
    for (unsigned i = 0; i < s->points; ++i) {
        const float d2 = s->p[i][0] * s->p[i][0] + s->p[i][1] * s->p[i][1];
        r2 = d2 > r2 ? d2 : r2;
    }
    s->extent = scalar::sqrt(r2);
}
// The strike's geometry memoised (the performance pass: a strike is drawn on three frames or more; a pure function of
// its key, so an entry never goes stale): ion_cache_sets sets of ion_cache_ways entries, the set by a hash of (sequence,
// strike), the least recently used way replaced (review 2026-11-07: 32 direct-mapped slots recomputed nearly every
// strike at 64 lit hits; 256 entries hold 64 lit hits with their flash's and afterglow's strikes, about 560 KB in the
// TU's .bss at 6 channels of 3 branches).
// Sequence 0 is no record (an empty entry). The game thread's only (the stage's caller); a reference stays valid until
// the next lookup.
constexpr unsigned ion_cache_sets = 64, ion_cache_ways = 4;
struct IonCacheEntry {
    std::uint32_t sequence = 0, strike = 0, used = 0;
    IonStrike st;
};
inline const IonStrike& ion_strike_cached(std::uint32_t sequence, unsigned strike) noexcept {
    static IonCacheEntry cache[ion_cache_sets * ion_cache_ways];
    static std::uint32_t clock = 0;
    IonCacheEntry* set = cache + (mix32(sequence * 8u + strike) & (ion_cache_sets - 1u)) * ion_cache_ways;
    IonCacheEntry* victim = set;
    ++clock;
    for (unsigned w = 0; w < ion_cache_ways; ++w) {
        IonCacheEntry& c = set[w];
        if (c.sequence == sequence && c.strike == strike && sequence) {
            c.used = clock;
            return c.st;
        }
        if (c.used < victim->used) victim = &c;
    }
    ion_strike(sequence, strike, &victim->st);
    victim->sequence = sequence;
    victim->strike = strike;
    victim->used = clock;
    return victim->st;
}
// The ion's strike timer of a held record (hull-impact-classes.md 4.5, 4.6): ticks refresh its t0 in place, so the
// hold has lasted t0 - first_ms; the strike schedule restarts every ion_strike_span_ms of the hold (its strikes
// re-seeded, cycle `*cycle`), and the schedule's remaining strikes, afterglow and fade run to ion_life_ms after the hold
// ends. A record that was never refreshed (t0 at or before its first stage frame) is at cycle 0: ion_phase's own age.
inline std::uint32_t ion_hold_age(const core::Event& e, std::uint32_t first_ms, std::uint32_t age_ms,
                                  unsigned* cycle) noexcept {
    const std::int32_t held = std::int32_t(e.t0_game_ms - first_ms);
    const std::uint32_t k = held > 0 ? std::uint32_t(held) / core::hull::ion_strike_span_ms : 0u;
    const std::uint32_t base = k * core::hull::ion_strike_span_ms;
    *cycle = unsigned(k);
    return age_ms > base ? age_ms - base : 0u;
}
// The strike index of cycle k's strike j (IonJob::strike is a byte; the channel sets repeat after 31 cycles, 29.8 s)
inline unsigned ion_cycle_strike(unsigned cycle, unsigned strike) noexcept {
    static_assert(core::hull::ion_strikes_max * 31u <= 255u, "a cycle's strike index is a byte");
    return strike + core::hull::ion_strikes_max * (cycle % 31u);
}
// What a hit draws at game age `age` (ms; floor_ms the record's three-frame floor): the latest started strike's flash
// while it is lit, the afterglow of the strike before it (or of the latest once its flash is over; the last strike's to
// the life's end), both x the life's fade, and the cold light's factor; e its radiance factors (x ion_channel_e), w the
// width factors.
struct IonPhase {
    bool lit = false, flash = false, after = false;
    unsigned strike = 0, after_strike = 0, latest = 0;
    float e = 0.f, w = 1.f, after_e = 0.f, after_w = 1.f, glow = 0.f, fade = 0.f;
};
inline IonPhase ion_phase(std::uint32_t sequence, float age, float floor_ms) noexcept {
    namespace hl = core::hull;
    IonPhase ph;
    const float L = float(hl::ion_life_ms);
    if (!(age >= 0.f) || !(age < L)) return ph;
    const float span = float(hl::ion_strike_span_ms);
    const float flash_life = float(hl::ion_strike_ms) > floor_ms ? float(hl::ion_strike_ms) : floor_ms;
    // the latest started strike j at t, its period p; the one before at t_prev
    float t = 0.f, t_prev = 0.f, p = ion_period(sequence, 0, floor_ms);
    unsigned j = 0;
    while (j + 1 < hl::ion_strikes_max && t + p < span && t + p <= age) {
        t_prev = t;
        t += p;
        p = ion_period(sequence, ++j, floor_ms);
    }
    const float x0 = hl::ion_fade_from * L;
    ph.fade = age < x0 ? 1.f : (L - age) / (L - x0);
    auto e_of = [](unsigned i, float ti) {
        return ti < float(hl::ion_late_ms) ? 1.f - hl::ion_strike_decay * float(i) : hl::ion_late_e;
    };
    auto w_of = [](float ti) { return ti < float(hl::ion_late_ms) ? 1.f : hl::ion_late_w; };
    const float ej = e_of(j, t);
    if (age < t + flash_life) {
        ph.flash = true;
        ph.strike = j;
        ph.e = ej * ph.fade;
        ph.w = w_of(t);
        if (j) {
            ph.after = true;
            ph.after_strike = j - 1;
            ph.after_e = hl::ion_after_e * e_of(j - 1, t_prev) * ph.fade;
            ph.after_w = w_of(t_prev);
        }
    } else { // between strikes (and after the last one's flash, to the life's end): its afterglow
        ph.after = true;
        ph.after_strike = j;
        ph.after_e = hl::ion_after_e * ej * ph.fade;
        ph.after_w = w_of(t);
    }
    ph.lit = ph.fade > 0.f;
    ph.latest = j;
    float x = (age - t) / p;
    x = x < 1.f ? x : 1.f;
    ph.glow = ej * (hl::ion_glow_floor + (1.f - hl::ion_glow_floor) * (1.f - x)) * ph.fade;
    return ph;
}
// The stage's tangent plane of a hit (owner frame): normal to the contact point's direction from the owner's origin
// (0, 0, 1 at the origin), its two axes taken into view through the owner's axes in view (frame rows). The vertex program
// turns it onto the lane's normal at the contact pixel; this plane is its fallback.
inline void ion_tangents(const std::int32_t pos[3], const float frame[9], float t1[3], float t2[3]) noexcept {
    float n[3] = {float(pos[0]), float(pos[1]), float(pos[2])};
    if (!normalize3(n)) {
        n[0] = n[1] = 0.f;
        n[2] = 1.f;
    }
    const float a[3] = {n[1] * n[1] < 0.81f ? 0.f : 1.f, n[1] * n[1] < 0.81f ? 1.f : 0.f, 0.f};
    float u[3], v[3];
    cross3(n, a, u);
    normalize3(u);
    cross3(n, u, v);
    for (unsigned j = 0; j < 3; ++j) {
        t1[j] = u[0] * frame[j] + u[1] * frame[3 + j] + u[2] * frame[6 + j];
        t2[j] = v[0] * frame[j] + v[1] * frame[3 + j] + v[2] * frame[6 + j];
    }
}
// One lightning segment's quad (renderer::WeaponLightningVertex; weapon_lightning_vs.hlsl, weapon_filament_ps.hlsl):
// the hit's contact point c (view) with the radiance e, the segment's ends pa, pb in the plane of the axes t1, t2 (view
// units a plane unit), the core's half width hw (px) and the depth window tol (view units); the vertex program orients
// the quad (corner k: end A for k 0 and 2, B for 1 and 3, by the sign of c[3]; side + for 0 and 1, - for 2 and 3, by
// the sign of d[3]). The performance pass: four 16-byte stores a vertex, nothing computed.
inline void ion_segment_quad(const float c[3], const float t1[3], const float t2[3], const float pa[2], const float pb[2],
                             float hw, float e, float tol, renderer::WeaponLightningVertex* out) noexcept {
    const __m128 va = _mm_setr_ps(c[0], c[1], c[2], e), vb = _mm_setr_ps(pa[0], pa[1], pb[0], pb[1]);
    const __m128 cm = _mm_setr_ps(t1[0], t1[1], t1[2], -hw), cp = _mm_setr_ps(t1[0], t1[1], t1[2], hw);
    const __m128 dm = _mm_setr_ps(t2[0], t2[1], t2[2], -tol), dp = _mm_setr_ps(t2[0], t2[1], t2[2], tol);
    for (unsigned k = 0; k < 4; ++k) {
        _mm_storeu_ps(out[k].a, va);
        _mm_storeu_ps(out[k].b, vb);
        _mm_storeu_ps(out[k].c, (k & 1u) ? cp : cm);
        _mm_storeu_ps(out[k].d, (k & 2u) ? dm : dp);
    }
}
// A lit ion strike of the frame (the lightning's job, build_hull (C) and (C2); a hit's flash and afterglow two jobs,
// adjacent): its contact point and the stage's axes x
// r_hit (view), r_hit in pixels there, the core half widths and radiances of its channels and branches, the depth
// window, its age, sequence and strike, its visible segments (mask: by the strike's point index i, the segment i .. i + 1)
// and, once written, its first segment, its channels' ends (from first) and charged screens, the channels kept.
struct IonJob {
    static constexpr unsigned mask_words = (IonStrike::max_points + 31) / 32;
    float c[3], t1[3], t2[3];
    float r_px, hw_main, hw_branch, e_main, e_branch, tol;
    std::uint32_t age, sequence;
    std::uint16_t segments, first;
    std::uint8_t strike, own, kept, channels, keep_channels;
    std::uint8_t part; // 0 the hit's first job, 1 its second (the flash's then the afterglow's: cut together)
    std::uint16_t chan_end[core::hull::ion_channels_hi];
    float chan_area[core::hull::ion_channels_hi];
    std::uint32_t mask[mask_words];
};
// One segment's four vertices from a job's prepared rows (ion_segment_quad's layout; four 16-byte stores a vertex).
inline void ion_emit(renderer::WeaponLightningVertex* out, __m128 va, __m128 vb, __m128 cm, __m128 cp, __m128 dm,
                     __m128 dp) noexcept {
    for (unsigned k = 0; k < 4; ++k) {
        _mm_storeu_ps(out[k].a, va);
        _mm_storeu_ps(out[k].b, vb);
        _mm_storeu_ps(out[k].c, (k & 1u) ? cp : cm);
        _mm_storeu_ps(out[k].d, (k & 2u) ? dm : dp);
    }
}
// A flash's quad (the sprite batch's disc kind): the core core_frac r_hit (>= floor_flash_px) in a halo of halo_frac
// r_hit, centred on c (view) biased towards the eye by bias r_hit, the halo's colour, its radiance, and the core's share
// of the halo colour (c.z: the core lerp(white, halo, core_share); 0 white, the beam spot's beam_core_mix / beam_glow_mix).
// False when behind the near plane or past the cap (*capped).
inline bool flash_quad(const HullView& f, const Screen& sc, const float c_view[3], float r_hit, float core_frac,
                       float halo_frac, const float halo[3], float radiance, renderer::WeaponSpriteVertex* v,
                       float* screens, float core_share = 0.f) noexcept {
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
        v[q].c[0] = v[q].c[1] = 0.f;
        v[q].c[2] = core_share;
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
// The beam spot's weapon colour applies (section 4.5, review 2026-11-07: only where a colour exists): the tint from the
// light or the texture average, its 8-bit saturation (max - min) / max above hull::beam_colour_sat_min; white and grey
// beams (Mayhem 3's PBC, stock PAL / FUSIONBEAM / TRIBEAM) keep the heat look.
inline bool beam_coloured(const core::Event& e) noexcept {
    if (e.tint_source != core::tint_light && e.tint_source != core::tint_texture) return false;
    if (!(e.tint_rgb & core::rgb_present)) return false;
    const unsigned r = e.tint_rgb & 0xffu, g = (e.tint_rgb >> 8) & 0xffu, b = (e.tint_rgb >> 16) & 0xffu;
    const unsigned hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return hi > 0 && float(hi - lo) > core::hull::beam_colour_sat_min * float(hi);
}
// The record's element reach (view units), from its class constants, calibre and tier unit (2026-11-07): the
// owner-attached elements about the contact point (the flash halo, the heavy streak, the puff's R_b, the ring's
// ring_stretch (rr + 3 w), the ion's lightning (ion_reach_r), the shimmer square, the glow and the ember; the largest) and the sparks
// about their origin (per set the launch speed x (1 + spread) x tau, which bounds tau (1 - e^(-t / tau)), the curl drift
// spark_curl_x of it over the longest life (law x spark_life_hi, or the frame floor), and the streak's world part:
// streak_cap rest lengths of spark_rest_w widths at the largest width and length factors, plus half a width).
inline void record_reach(const core::Event& e, const HullTuning& t, float frame_floor_ms, float* attached,
                         float* spark) noexcept {
    namespace hl = core::hull;
    const unsigned a = anatomy_of(e);
    const float k = record_calibre(e);
    const float U = hull_unit(e, t), r_hit = hl::r_scale * U * (hl::r_base + hl::r_k * k);
    const float* fr = hl::class_flash[a];
    float m = fr[1] > hl::streak_fallback_halo ? fr[1] : hl::streak_fallback_halo; // the star's arm
    const float streak = (hl::streak_front > hl::streak_back ? hl::streak_front : hl::streak_back) + hl::streak_width;
    const float* rg = hl::class_ring[a];
    const float* gw = hl::class_glow[a];
    const float* em = hl::class_ember[a];
    // the ring's front stretched along the heading (the shock front, ring_stretch) reaches that much further
    const float ring = hl::ring_stretch * (rg[0] + 3.f * rg[1]);
    for (const float v : {streak, ring, a == hl::an_ion ? hl::ion_glow_r * hl::ion_reach_r : 0.f, hl::class_shimmer[a], gw[0], em[0]})
        m = v > m ? v : m;
    // the burst (its lobed sphere biased bias r towards the eye), its glow (glow_r_x R(t) <= glow_r_x R_b), its smoke
    const float* pf = hl::class_puff[a];
    const float R_b = pf[0] * r_hit + U * (pf[1] + pf[2] * k);
    float burst = 0.f;
    if (R_b > 0.f) {
        const float body = R_b * (1.f + core::look::fireball_lobe_amp) + hl::bias * r_hit;
        const float glow = hl::glow_r_x * R_b, smoke = hl::class_smoke[a][0] * R_b * (1.f + core::look::fireball_lobe_amp);
        burst = body > glow ? body : glow;
        burst = smoke > burst ? smoke : burst;
    }
    *attached = m * r_hit + burst;
    float sp = 0.f;
    for (unsigned set = 0; set < 3; ++set) {
        const SparkSet ss = spark_set(a, set);
        if (!(ss.n0 + ss.nk * k > 0.f)) continue;
        const float speed = ss.speed * U * (1.f + ss.speed_spread);
        float life_s = (ss.life + ss.life_k * k) * hl::spark_life_hi;
        life_s = (life_s > frame_floor_ms ? life_s : frame_floor_ms) * 1e-3f;
        const float width = ss.width * U * hl::spark_width_hi, len_x = hl::spark_len_hi * ss.rest_w / hl::spark_rest_w;
        const float r = speed * (ss.tau + hl::spark_curl_x * life_s) +
                        core::look::streak_cap * hl::spark_rest_w * width * len_x + 0.5f * width;
        sp = r > sp ? r : sp;
    }
    *spark = sp;
}
// The pixel floors' reach (px) past any world bound: a spark streak's floored rest length (streak_cap x (spark_rest_w
// floor_lit_px + floor_len_px) x the largest length factor, half the lit floor), the star's floored half (star_floor_px
// and half the width floor) and a beam spot core's floored halo (half floor_flash_px x spot_halo over spot_core).
constexpr float reach_floor_px() noexcept {
    namespace hl = core::hull;
    float m = 0.f;
    for (unsigned r = 0; r < 30; ++r) {
        const float len_x = hl::spark_len_hi * hl::class_sparks[r][7] / hl::spark_rest_w;
        const float v = core::look::streak_cap * (hl::spark_rest_w * core::look::floor_lit_px + core::look::floor_len_px) *
                            len_x + 0.5f * core::look::floor_lit_px;
        m = v > m ? v : m;
    }
    const float star = hl::star_floor_px + 0.5f * hl::star_width_px;
    const float spot = 0.5f * core::look::floor_flash_px * hl::spot_halo / hl::spot_core;
    m = star > m ? star : m;
    return spot > m ? spot : m;
}
// Whether a view-space sphere grown by the pixel floors at its far depth is outside the view volume (an element there
// cannot reach a pixel). px: view units per pixel at depth 1 (the larger of the two axes').
inline bool out_of_reach(const float c[3], float R, const HullView& f, float far_z, float px) noexcept {
    const float z = c[2] + R > f.near_z ? c[2] + R : f.near_z;
    return !sphere_in_view(c, R + reach_floor_px() * px * z, f.cam, f.near_z, far_z);
}
// Builds the frame's hull-hit elements (see the header) into the buffers. Returns the counts. visible (nullable): adds
// every kept owner (hull::max_owners) with a hull hit it read that is in reach (its reach sphere, or a record's spark
// sphere, reaches the view volume: the cull's own test, whether or not HullTuning::reach_cull applies it) as drawn and
// in view, a kept shield-only owner whose owner_in_view sphere reaches it as in view, and every owner past the cap by
// the same tests on its located position (stage::locate_owner) as in view, for the stamp (core::ring_stamp_hidden).
// Internal linkage (static), so its ring-sized static scratch is the TU's zero-initialised .bss, not a comdat .data$
// section of the file (the production DLL calls it from one TU).
static inline HullCounts build_hull(core::Ring& ring, HullState& state, Reader rd, const HullView& f, const HullTuning& t,
                             HullBuffers& out, core::VisibleOwners* visible = nullptr) noexcept {
    namespace hl = core::hull;
    HullCounts n{};
    unsigned nq = 0, ng = 0, nb = 0, nr = 0, nf = 0, ns = 0, nm = 0;
    out.sprite_quads = out.glow_boxes = out.fireball_boxes = out.ring_quads = out.filament_boxes = out.scorch_boxes =
        out.smoke_boxes = out.shimmer_count = 0;
    auto cap_of = [](const void* p, unsigned cap, unsigned most) { return p ? (cap < most ? cap : most) : 0u; };
    const unsigned sprite_cap = cap_of(out.sprites, out.sprite_cap, renderer::weapon_sprite_quads);
    const unsigned glow_cap = cap_of(out.glows, out.glow_cap, renderer::weapon_glow_boxes);
    const unsigned fireball_cap = cap_of(out.fireballs, out.fireball_cap, renderer::weapon_fireball_boxes);
    const unsigned ring_cap = cap_of(out.rings, out.ring_cap, renderer::weapon_ring_quads);
    const unsigned filament_cap = cap_of(out.filaments, out.filament_cap, renderer::weapon_filament_boxes);
    const unsigned scorch_cap = cap_of(out.scorches, out.scorch_cap, renderer::weapon_scorch_boxes);
    const unsigned smoke_cap = cap_of(out.smokes, out.smoke_cap, renderer::weapon_smoke_boxes);
    const unsigned shimmer_cap = cap_of(out.shimmer, out.shimmer_cap, hl::shimmer_max);
    if (!(f.width > 0.f) || !(f.height > 0.f) || !(f.cam.m00 > 0.f) || !(f.cam.m11 > 0.f)) return n;
    const float frame_floor_ms = float(t.min_frames) * float(f.step_ms);
    const std::uint32_t hold_window = hl::hold_ms > 2u * f.step_ms ? hl::hold_ms : 2u * f.step_ms;
    const float engine_per_view = 1.f / core::world_scale;
    // (A) The hold rule (section 3.9): every stage-owned beam, repair or tug record re-lights a spot of its owner and
    // (table, row) within hold_r r, or becomes one; either way it leaves the ring (one record per beam per frame). A
    // holding record of another anatomy (an ion beam) refreshes its held ring record instead (one record per beam).
    if (t.spots) {
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            const core::Event& e = ring.slots[i];
            if (!e.sequence || e.role != core::role_hull_hit || !(e.flags & core::flag_stage)) continue;
            if (!holds(e)) continue;
            const unsigned a = anatomy_of(e);
            const float r_eng = hull_r_hit(e, t) * engine_per_view, hold2 = hl::hold_r * r_eng * hl::hold_r * r_eng;
            if (!spot_anatomy(a)) {
                // Held in the ring (a pinned class with the beam bits: the ion's lightning, (C)): the newest tick
                // re-poses the older held record of its owner and (table, row) within hold_r r (its sequence, first
                // stage frame and strike seed kept; its t0 the tick's, so the hold's length is t0 - first_ms) and
                // leaves the ring; none held: it stays as the hold's record.
                core::Event* into = nullptr;
                for (unsigned j = 0; j < core::ring_capacity; ++j) {
                    core::Event& o = ring.slots[j];
                    if (j == i || !o.sequence || o.role != core::role_hull_hit || !(o.flags & core::flag_stage) ||
                        o.owner != e.owner || o.node != e.node || o.handle != e.handle || o.table != e.table ||
                        o.row != e.row || std::int32_t(e.sequence - o.sequence) <= 0 || !holds(o) || anatomy_of(o) != a)
                        continue;
                    if (std::int32_t(f.now - o.t0_game_ms) > std::int32_t(hold_window)) continue;
                    float d2 = 0.f;
                    for (unsigned k = 0; k < 3; ++k) {
                        const float d = float(core::wrap_sub(e.pos[k], o.pos[k]));
                        d2 += d * d;
                    }
                    if (d2 <= hold2 && (!into || std::int32_t(o.t0_game_ms - into->t0_game_ms) > 0)) into = &o;
                }
                if (into) {
                    const std::uint32_t seq = into->sequence;
                    if (std::int32_t(e.t0_game_ms - into->t0_game_ms) > 0) into->t0_game_ms = e.t0_game_ms;
                    for (unsigned k = 0; k < 3; ++k) {
                        into->pos[k] = e.pos[k];
                        into->dir[k] = e.dir[k];
                    }
                    into->damage = e.damage;
                    into->shield_q = e.shield_q;
                    into->sequence = seq;
                    ++n.merges;
                    core::ring_release(ring, i);
                    state.slots[i] = HullSlot{};
                }
                continue;
            }
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
    // (B) The owners (review 3; visible first 2026-11-07): every owner of a stage-owned record, whether the previous
    // frame stamped one of its records off screen (core::hidden_off_screen: such owners after the others, which are on
    // screen or new this frame), whether it has a hull hit (a shield-only owner after them) and its newest hull hit's
    // age (t0, game ms); past hull::max_owners the last in that order are not drawn this frame (ties by the object
    // address), a stable choice from frame to frame (no blinking: an on-screen owner past the cap is still stamped on
    // screen). The owner map finds a record's candidate in one probe or two.
    struct Candidate {
        std::uint32_t owner, node, handle, newest, index; // index: a record of the owner (the reader's validation)
        bool hull;
        std::uint8_t off;
        float reach; // the largest record reach (attached + spark) of its hull hits
    };
    // The ring-sized scratch (here and the flashes' bookkeeping below: about 45 KB at 1024 records) is static, off the
    // game thread's stack (1 MB reserve), as the glue's frame buffers; the game thread is the only caller and nothing
    // carries over between calls.
    static Candidate cand[core::ring_capacity];
    static OwnerMap map;
    map.clear();
    unsigned cand_count = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        const core::Event& e = ring.slots[i];
        if (!e.sequence || !(e.flags & core::flag_stage) || (e.role != core::role_hull_hit && e.role != core::role_shield_hit))
            continue;
        const bool is_hull = e.role == core::role_hull_hit;
        if (is_hull && state.slots[i].sequence == e.sequence && state.slots[i].orphan) continue; // needs no owner
        const std::uint32_t age = f.now - e.t0_game_ms;
        const unsigned h = map.find(e.owner);
        Candidate* c;
        if (!map.value[h]) {
            map.key[h] = e.owner;
            map.value[h] = std::uint16_t(cand_count + 1);
            c = &cand[cand_count++];
            *c = Candidate{e.owner, e.node, e.handle, 0xffffffffu, i, false, 0, 0.f};
        } else {
            c = &cand[map.value[h] - 1];
        }
        if (is_hull) { // the record's reach, once at its first stage frame (the slot as (C) would start it)
            HullSlot& hs = state.slots[i];
            if (hs.sequence != e.sequence) {
                hs = HullSlot{};
                hs.sequence = e.sequence;
            }
            if (hs.reach_floor_ms < 0.f) { // the latch below recomputes it when its floor differs
                record_reach(e, t, frame_floor_ms, &hs.reach_attached, &hs.reach_spark);
                hs.reach_floor_ms = frame_floor_ms;
            }
            const float r = hs.reach_attached + hs.reach_spark;
            c->reach = r > c->reach ? r : c->reach;
        }
        c->off = std::uint8_t(c->off | (ring.hidden[i] & core::hidden_off_screen));
        if (is_hull && (!c->hull || age < c->newest)) {
            c->newest = age;
            c->index = i;
        }
        c->hull = c->hull || is_hull;
    }
    auto before = [](const Candidate& a, const Candidate& b) {
        if (a.off != b.off) return a.off < b.off;
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
    // Out of reach (2026-11-07): an owner with a hull hit whose sphere of 2 r + its records' largest reach, grown by the
    // pixel floors, lies outside the view volume (the far plane included; core::VisibleOwners::far_margin past it). A
    // kept one keeps its track and builds no elements (HullTuning::reach_cull), but a record of it still builds when its
    // spark sphere (below, in (C)) reaches the view; the stamp takes the same verdict (in reach: the owner sphere or a
    // record's spark sphere).
    const float far_z = visible ? visible->far_z : 0.f;
    const float px_scale = 0.5f * f.width * f.cam.m00 < 0.5f * f.height * f.cam.m11 ? 0.5f * f.width * f.cam.m00
                                                                                      : 0.5f * f.height * f.cam.m11;
    const float px_per_z = px_scale > 0.f ? 1.f / px_scale : 0.f; // view units per pixel at depth 1
    auto owner_out_of_reach = [&](const Owner& o, float reach) {
        const double s = double(core::world_scale);
        const double w[3] = {double(o.pos[0]) * s, double(o.pos[1]) * s, double(o.pos[2]) * s};
        float c[3];
        to_view(f.cam, w, c);
        return out_of_reach(c, 2.f * float(double(o.radius > 0 ? o.radius : 0) * s) + reach, f, far_z, px_per_z);
    };
    for (unsigned k = keep; k < cand_count; ++k) {
        n.owners_dropped += cand[k].hull;
        // located for the stamp only (the next frame's order); read once a frame for both stages
        if (!visible) continue;
        Owner o;
        if (locate_owner(rd, ring.slots[cand[k].index], *visible, &o) != OwnerStatus::ok) continue;
        if (cand[k].hull ? !owner_out_of_reach(o, cand[k].reach) : owner_in_view(o, f.cam, f.near_z, visible->far_z))
            visible->add_in_view(cand[k].owner);
    }
    struct Seen {
        std::uint32_t owner, node, handle;
        OwnerStatus status;
        Owner data;
        float vel[3];
        float centre[3], frame[9]; // its frame in view (owner_frame), once per frame when a filament needs it
        std::uint8_t frame_state;  // 0 not taken, 1 valid, 2 degenerate
        std::uint8_t out;          // its reach sphere out of reach this frame (a kept owner with a hull hit)
        std::uint8_t reached;      // a record of it out of reach by the owner's sphere whose spark sphere reaches
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
        remember_owner(visible, ring.slots[cand[k].index], sn.status, sn.data);
        sn.vel[0] = sn.vel[1] = sn.vel[2] = 0.f;
        sn.frame_state = 0;
        sn.out = sn.reached = 0;
        if (sn.status != OwnerStatus::ok) continue;
        if (cand[k].hull) {
            sn.out = owner_out_of_reach(sn.data, cand[k].reach) ? 1 : 0;
            n.owners_out_of_reach += t.reach_cull ? sn.out : 0u;
        }
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
        sn.out = sn.reached = 0;
        return &sn;
    };
    // The cluster saturation (hull-burst-look.md section 3): each kept owner's live burst records (their owner-frame
    // point and R_b in engine units) bucketed by owner (a counting sort over static scratch, no allocation); a record's
    // N = 1 + the sum over its owner's other live bursts within cluster_r_x of its own R_b of their remaining life
    // fractions (review 2026-11-07: a neighbour fades out of N with its life instead of stepping N down at its end).
    // Per owner the neighbours come from a uniform grid of the owner's largest reach (each record's 27 cells about its
    // own, the exact distance test kept; a counted cell's records only, so a hash collision never counts twice): O(n)
    // per owner for spread hits, the pairwise loop under HullTuning::cluster_grid_min records (cheaper there: 64 measured
    // 2.2 us under the grid). N stops at cluster_n_cap (a dense pile's scan bound). An owner out of reach is skipped.
    static float cluster_n[core::ring_capacity];
    static std::uint16_t burst_rec[core::ring_capacity], burst_sorted[core::ring_capacity];
    static std::uint8_t burst_owner[core::ring_capacity];
    static float burst_reach2[core::ring_capacity], burst_weight[core::ring_capacity];
    if (t.cluster) {
        constexpr unsigned grid_buckets = 2048, no_cell = 0xffffu;
        constexpr float cluster_n_cap = 64.f;
        static_assert(grid_buckets >= 2 * core::ring_capacity && (grid_buckets & (grid_buckets - 1)) == 0, "grid");
        static std::int32_t cell_of[core::ring_capacity][3];
        static std::uint16_t bucket_head[grid_buckets], cell_next[core::ring_capacity];
        static std::uint32_t bucket_stamp[grid_buckets], grid_stamp = 0;
        unsigned owner_bursts[hl::max_owners + 1] = {}, start[hl::max_owners + 1] = {};
        unsigned nbr = 0;
        // the kept owners by address (open addressing, 64 slots for at most 32: a record's owner in O(1), not a scan of
        // the kept list per record); an owner out of reach draws no burst (its reach holds the bursts'), so its records
        // count for nothing and are not entered
        constexpr unsigned owner_slots = 64;
        static_assert(owner_slots >= 2 * hl::max_owners, "owner slots");
        std::uint32_t slot_owner[owner_slots];
        std::uint8_t slot_ko[owner_slots];
        for (unsigned q = 0; q < owner_slots; ++q) slot_ko[q] = 0xff;
        auto owner_hash = [](std::uint32_t o) { return ((o >> 3) * 2654435761u) >> 26; };
        for (unsigned k = 0; k < keep; ++k) {
            if (seen[k].status != OwnerStatus::ok || (t.reach_cull && seen[k].out)) continue;
            unsigned q = owner_hash(seen[k].owner);
            while (slot_ko[q] != 0xff && slot_owner[q] != seen[k].owner) q = (q + 1) & (owner_slots - 1);
            if (slot_ko[q] == 0xff) {
                slot_owner[q] = seen[k].owner;
                slot_ko[q] = std::uint8_t(k);
            }
        }
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            cluster_n[i] = 1.f;
            const core::Event& e = ring.slots[i];
            if (!e.sequence || e.role != core::role_hull_hit || !(e.flags & core::flag_stage)) continue;
            const HullSlot& hs = state.slots[i];
            if (hs.sequence != e.sequence || hs.orphan) continue;
            unsigned q = owner_hash(e.owner);
            while (slot_ko[q] != 0xff && slot_owner[q] != e.owner) q = (q + 1) & (owner_slots - 1);
            if (slot_ko[q] == 0xff) continue;
            const unsigned ko = slot_ko[q];
            const unsigned a = anatomy_of(e);
            const float* pf = hl::class_puff[a];
            const float k = record_calibre(e);
            const float U = hull_unit(e, t), r_hit = hl::r_scale * U * (hl::r_base + hl::r_k * k);
            const float R_b = pf[0] * r_hit + U * (pf[1] + pf[2] * k);
            if (!(R_b > 0.f)) continue;
            const std::uint32_t age = hs.latched ? f.now - hs.first_ms : 0u;
            bool fl = false;
            const float life = element_life_ms(pf[3] + pf[4] * k, element_hash(e.sequence, 3), t.stagger,
                                               hs.latched ? hs.floor_ms : frame_floor_ms, &fl);
            if (age > 60000u || !(float(age) < life)) continue;
            const float reach = hl::cluster_r_x * R_b * engine_per_view;
            burst_rec[nbr] = std::uint16_t(i);
            burst_owner[nbr] = std::uint8_t(ko);
            burst_reach2[i] = reach * reach;
            burst_weight[i] = t.cluster_fade ? 1.f - float(age) / life : 1.f;
            ++owner_bursts[ko];
            ++nbr;
        }
        for (unsigned k = 1; k <= keep; ++k) start[k] = start[k - 1] + owner_bursts[k - 1];
        for (unsigned b = 0; b < nbr; ++b) burst_sorted[start[burst_owner[b]]++] = burst_rec[b];
        auto within = [&](unsigned i, unsigned j) {
            const std::int32_t* pi = ring.slots[i].pos;
            const std::int32_t* pj = ring.slots[j].pos;
            const float dx = float(core::wrap_sub(pi[0], pj[0])), dy = float(core::wrap_sub(pi[1], pj[1]));
            const float dz = float(core::wrap_sub(pi[2], pj[2]));
            return dx * dx + dy * dy + dz * dz <= burst_reach2[i];
        };
        auto bucket_of = [](std::int32_t x, std::int32_t y, std::int32_t z) {
            return (std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u ^ std::uint32_t(z) * 83492791u) &
                   (grid_buckets - 1);
        };
        unsigned at = 0;
        for (unsigned k = 0; k < keep; ++k) {
            const unsigned end = at + owner_bursts[k];
            if (end - at < t.cluster_grid_min) {
                for (unsigned b = at; b < end; ++b) {
                    const unsigned i = burst_sorted[b];
                    float sum = 1.f;
                    for (unsigned c = at; c < end; ++c)
                        if (c != b && within(i, burst_sorted[c])) sum += burst_weight[burst_sorted[c]];
                    cluster_n[i] = sum < cluster_n_cap ? sum : cluster_n_cap;
                }
                at = end;
                continue;
            }
            float cell = 0.f;
            for (unsigned b = at; b < end; ++b) cell = burst_reach2[burst_sorted[b]] > cell ? burst_reach2[burst_sorted[b]] : cell;
            cell = scalar::sqrt(cell);
            const float inv = cell > 0.f ? 1.f / cell : 0.f;
            const std::int32_t* origin = ring.slots[burst_sorted[at]].pos;
            ++grid_stamp;
            for (unsigned b = at; b < end; ++b) {
                const unsigned i = burst_sorted[b];
                for (unsigned q = 0; q < 3; ++q) {
                    const float x = float(core::wrap_sub(ring.slots[i].pos[q], origin[q])) * inv;
                    std::int32_t c = std::int32_t(x);
                    c -= float(c) > x; // floor without libm
                    cell_of[i][q] = c;
                }
                const std::uint32_t h = bucket_of(cell_of[i][0], cell_of[i][1], cell_of[i][2]);
                if (bucket_stamp[h] != grid_stamp) {
                    bucket_stamp[h] = grid_stamp;
                    bucket_head[h] = std::uint16_t(no_cell);
                }
                cell_next[i] = bucket_head[h];
                bucket_head[h] = std::uint16_t(i);
            }
            for (unsigned b = at; b < end; ++b) {
                const unsigned i = burst_sorted[b];
                const std::int32_t* ci = cell_of[i];
                float sum = 1.f;
                for (int dz = -1; dz <= 1 && sum < cluster_n_cap; ++dz)
                    for (int dy = -1; dy <= 1 && sum < cluster_n_cap; ++dy)
                        for (int dx = -1; dx <= 1 && sum < cluster_n_cap; ++dx) {
                            const std::int32_t cx = ci[0] + dx, cy = ci[1] + dy, cz = ci[2] + dz;
                            const std::uint32_t h = bucket_of(cx, cy, cz);
                            if (bucket_stamp[h] != grid_stamp) continue;
                            for (unsigned j = bucket_head[h]; j != no_cell; j = cell_next[j])
                                if (j != i && cell_of[j][0] == cx && cell_of[j][1] == cy && cell_of[j][2] == cz && within(i, j))
                                    sum += burst_weight[j];
                        }
                cluster_n[i] = sum < cluster_n_cap ? sum : cluster_n_cap;
            }
            at = end;
        }
    }
    // The budget's bookkeeping per box.
    static_assert(renderer::weapon_glow_boxes <= 0xffff && renderer::weapon_fireball_boxes <= 0xffff, "box indices");
    BoxMeta glow_meta[renderer::weapon_glow_boxes];
    BoxMeta burst_meta[renderer::weapon_fireball_boxes];
    BoxMeta ring_meta[renderer::weapon_ring_quads];
    // The lightning's jobs (the lit ion strikes with a visible segment, the first pass in (C), two a hit; the TU's .bss,
    // about 290 KB at 1024 records) and their order by age (the cap's and the shed's).
    constexpr unsigned ion_job_capacity = 2 * core::ring_capacity; // a hit's flash and afterglow
    static IonJob ion_jobs[ion_job_capacity];
    static std::uint16_t ion_order[ion_job_capacity];
    unsigned n_ion_jobs = 0;
    BoxMeta scorch_meta[renderer::weapon_scorch_boxes];
    BoxMeta smoke_meta[renderer::weapon_smoke_boxes];
    ShimmerRect shimmer_cand[16];
    float shimmer_screens[16];
    unsigned shimmer_cands = 0;
    const Camera& prev = f.prev_valid ? f.prev : f.cam;
    const float step_s = float(f.step_ms) * 1e-3f;
    const Screen sc = screen_of(f);
    unsigned sparks_left = hl::max_sparks;
    float flash_area = 0.f; // the flashes' charge (the shed order's last step: other owners' flashes, oldest first)
    // The flashes' bookkeeping (their quad, age, owner): the emitter reads the record's while it draws.
    constexpr unsigned flash_meta_cap = core::ring_capacity + hl::max_spots;
    static BoxMeta flash_meta[flash_meta_cap];
    static std::uint16_t flash_quad_of[flash_meta_cap];
    unsigned nfl = 0;
    std::uint32_t flash_age = 0, flash_owner = 0;
    bool flash_own = false;
    const float fine_px = core::look::fireball_fine_px * f.height * (1.f / 1440.f);
    const float fine_band_px = t.fine_band_px * f.height * (1.f / 1440.f);
    static float burst_fine[renderer::weapon_fireball_boxes]; // each burst box's octave weight 0 .. 2 (the steps (0b), (1))
    // each burst box's tongue share (fire x w_px, 0 .. 1: its charge's fire part) and view depth (the step (0c))
    static float burst_fire[renderer::weapon_fireball_boxes], burst_depth[renderer::weapon_fireball_boxes];
    const float fine4_px = core::look::fireball_fine4_px * f.height * (1.f / 1440.f),
                fine4_band_px = core::look::fireball_fine4_band_px * f.height * (1.f / 1440.f);
    // a box screen's charge by its octave weight: w2 + (w3 - w2) min(fine, 1) + (w4 - w3) max(fine - 1, 0)
    const auto octave_weight = [](float fine) {
        namespace lk = core::look;
        const float f3 = fine < 1.f ? fine : 1.f, f4 = fine > 1.f ? fine - 1.f : 0.f;
        return lk::budget_weight_fireball_coarse + (lk::budget_weight_fireball - lk::budget_weight_fireball_coarse) * f3 +
               (lk::budget_weight_fireball_fine4 - lk::budget_weight_fireball) * f4;
    };
    // The element emitters (each counts its cap).
    auto glow_box = [&](const float c[3], float R, const float colour[3], std::uint32_t age_ms, bool own, std::uint32_t owner,
                        float weight, const float* lobe = nullptr) -> bool {
        if (!(c[2] + R > f.near_z)) return false;
        float rect[4];
        bool full = false;
        if (!sphere_rect(f, c, R, rect, &full)) return false; // off screen
        if (ng >= glow_cap) {
            ++n.capped;
            return false;
        }
        glow_quad(f, c, R, rect, full, colour, out.glows + 4 * ng, lobe);
        glow_meta[ng] = BoxMeta{age_ms, owner, rect_screens(rect) * weight, std::uint8_t(own), 0};
        ++ng;
        return true;
    };
    // A burst body or its smoke (hull-burst-look.md sections 2 and 5): the sphere of the mean radius R (x (1 + lobe_amp)
    // for the lobes) about the hit biased bias r_hit towards the eye; its disc rotation, lobe seed and noise seed hashed
    // from the record (elements burst_rot_element, burst_lobe_element, 4); the third octave's weight 0 under fine_px, 1
    // past fine_px + fine_band_px of the projected R, linear between (charged between the two weights); x the erosion's
    // age fraction. Smoke: tint4 its darkening colour and -1, into the multiply boxes.
    // fire: the fire material's weight (hull::class_fire; 0 the previous law), gcore the white-hot core's gain, tg the
    // burst's game-time age in ms (the tongues' flow and flicker); none for a smoke or with the eye inside (fire-material.md 3)
    auto puff_box = [&](const float c_hit[3], float r_hit, float R, float T, float E, float x, float advect, std::uint32_t seq,
                        const float tint4[4], std::uint32_t age_ms, bool own, std::uint32_t owner, bool smoke,
                        float fire = 0.f, float gcore = 0.f, float tg = 0.f) -> bool {
        if (!(R > 0.f)) return false;
        const float dist = scalar::sqrt(dot3(c_hit, c_hit));
        const float bb = dist > 1e-6f ? 1.f - hl::bias * r_hit / dist : 1.f;
        const float c[3] = {c_hit[0] * bb, c_hit[1] * bb, c_hit[2] * bb};
        const float Rs = R * (1.f + core::look::fireball_lobe_amp);
        float rect[4];
        bool full = false;
        if (!(c[2] + Rs > f.near_z) || !sphere_rect(f, c, Rs, rect, &full)) return false;
        if (smoke ? nm >= smoke_cap : nb >= fireball_cap) {
            ++n.capped;
            return false;
        }
        // the octaves' weight 0 .. 2 (the third's, then from fireball_fine4_px the fourth's: 2026-11-07, the capital-size
        // bursts share the death body's program); a smoke and a burst with the eye inside (the box the whole screen) keep
        // three (explosion-look.md D49)
        float fine = 1.f, lod = 0.f;
        if (!full) {
            const float rpx = R * f.width * 0.5f * f.cam.m00 / c[2];
            const float a3 = rpx - fine_px, a4 = rpx - fine4_px;
            const float f3 = a3 <= 0.f ? 0.f : a3 >= fine_band_px ? 1.f : a3 / fine_band_px;
            const float f4 = a4 <= 0.f ? 0.f : a4 >= fine4_band_px ? 1.f : a4 / fine4_band_px;
            fine = f3 < 1.f ? f3 : 1.f + f4;
            lod = core::look::fire_lod(rpx * (1440.f / f.height), false);
        }
        const float fw = smoke || full ? 0.f : core::look::fire_enabled ? fire : (fire > 0.f ? -1.f : 0.f);
        const float core4[4] = {smoke ? 0.f : gcore, fw, lod, tg};
        const float fire_share = (fw > 0.f ? (fw < 1.f ? fw : 1.f) : 0.f) * (lod < 1.f ? lod : 1.f);
        const float look[4] = {T, E, x, 64.f * unit_of(element_hash(seq, 4))};
        const float disc[4] = {6.2831853f * unit_of(element_hash(seq, hl::burst_rot_element)),
                               64.f * unit_of(element_hash(seq, hl::burst_lobe_element)), advect,
                               smoke && fine > 1.f ? 1.f : fine};
        if (smoke) {
            fireball_quad(f, c, Rs, rect, full, look, tint4, disc, out.smokes + 4 * nm);
            smoke_meta[nm] = BoxMeta{age_ms, owner, rect_screens(rect) * core::look::budget_weight_smoke, std::uint8_t(own), 0};
            ++nm;
            ++n.smokes;
            return true;
        }
        fireball_quad(f, c, Rs, rect, full, look, tint4, disc, out.fireballs + 4 * nb, core4);
        // rank 1 marks a burst with a third octave for the budget's step (1) (the ranks are taken later); the tongues
        // charged x (budget_weight_fire - budget_weight_fireball) of their share
        burst_meta[nb] = BoxMeta{age_ms, owner,
                                 rect_screens(rect) * (octave_weight(fine) + (core::look::budget_weight_fire -
                                                                              core::look::budget_weight_fireball) * fire_share),
                                 std::uint8_t(own), std::uint8_t(fine > 0.f ? 1 : 0)};
        burst_fine[nb] = fine;
        burst_fire[nb] = fire_share;
        burst_depth[nb] = c[2];
        ++nb;
        ++n.bursts;
        n.fine += fine < 1.f;
        return true;
    };
    // A star (the hull hit's flash, hull-burst-look.md section 4) at its hashed angle, charged as a flash.
    auto emit_star = [&](const float c_view[3], float r_hit, float core_frac, float arm_r, std::uint32_t seq,
                         const float colour[3], float radiance) -> bool {
        if (nq >= sprite_cap) {
            if (c_view[2] > f.near_z) ++n.capped;
            return false;
        }
        float screens = 0.f;
        const float angle = 1.5707963f * unit_of(element_hash(seq, hl::star_element));
        if (!star_quad(f, sc, c_view, r_hit, core_frac, arm_r, angle, colour, radiance, out.sprites + 4 * nq, &screens))
            return false;
        flash_area += screens * hl::budget_weight_flash;
        if (nfl < flash_meta_cap) {
            flash_quad_of[nfl] = std::uint16_t(nq);
            flash_meta[nfl++] = BoxMeta{flash_age, flash_owner, screens * hl::budget_weight_flash, std::uint8_t(flash_own), 0};
        }
        ++nq;
        return true;
    };
    auto emit_flash = [&](const float c_view[3], float r_hit, float core_frac, float halo_frac, const float halo[3],
                          float radiance, float core_share = 0.f) -> bool {
        if (nq >= sprite_cap) {
            if (c_view[2] > f.near_z) ++n.capped;
            return false;
        }
        float screens = 0.f;
        if (!flash_quad(f, sc, c_view, r_hit, core_frac, halo_frac, halo, radiance, out.sprites + 4 * nq, &screens, core_share))
            return false;
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
            if (hs.reach_floor_ms != hs.floor_ms) { // its reach from the latched floor (first seen in another frame)
                record_reach(e, t, hs.floor_ms, &hs.reach_attached, &hs.reach_spark);
                hs.reach_floor_ms = hs.floor_ms;
            }
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
        // A held ion beam (the hold rule in the ring, (A)): its lightning's age on the strike timer, which also bounds
        // its life (ion_life_ms after the hold's last strike cycle); the other elements keep the hold's start.
        unsigned ion_cycle = 0;
        const bool ion_held = a == hl::an_ion && holds(e);
        const std::uint32_t life_age = ion_held ? ion_hold_age(e, hs.first_ms, age_ms, &ion_cycle) : age_ms;
        if (life_age > 60000u) continue; // a clock reset: Present releases it
        // Its anatomy's slowest element over (core::hull_lives, review 2026-11-07): the record leaves the ring at its own
        // end, not at the slowest class's (Present's hull_lifetime_ms bound stays for frames the stage does not draw).
        {
            const float life_end = float(core::hull_lives.v[a]) > floor_ms ? float(core::hull_lives.v[a]) : floor_ms;
            if (float(life_age) > life_end) {
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
        // Out of reach: the owner's sphere is (above); the sparks ride p0 + v_owner tau (1 - e^(-t / tau)), on the
        // segment p0 .. p0 + v t, so their sphere is its middle, half its length plus the spark reach.
        if (s && s->out) {
            const float t_s = float(f.now - hs.first_ms) * 1e-3f;
            double m[3];
            for (unsigned q = 0; q < 3; ++q) m[q] = hs.p0[q] + double(hs.v[q]) * double(0.5f * t_s);
            float c[3];
            to_view(f.cam, m, c);
            const float R = 0.5f * t_s * scalar::sqrt(dot3(hs.v, hs.v)) + hs.reach_spark;
            if (!out_of_reach(c, R, f, far_z, px_per_z))
                s->reached = 1;
            else if (t.reach_cull) {
                ++n.out_of_reach;
                continue;
            }
        }
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
                } else if (mode == 4) {
                    for (unsigned q = 0; q < 3; ++q) halo[q] = hl::cool_colour[q];
                } else
                    core::heat_colour(fr[3], halo);
                // the heavy hit without a heading: the longer arms instead of the streak
                const float arm_r = a == hl::an_heavy && !hs.has_axis ? hl::streak_fallback_halo : fr[1];
                if (emit_star(c_view, r_hit, fr[0], arm_r, e.sequence, halo, core::look::core_e * (1.f - age / life) * gain)) {
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
        // Burst: the fireball impostor at the contact point riding the owner (hull-burst-look.md section 2; the asteroid's
        // unlit dust cloud at share 1), scaled by its owner's cluster (section 3), and its smoke (section 5): from
        // smoke_t0 L_b over smoke_span L_b, the burst's noise and seeds, eroded over its own age y.
        const float* pf = hl::class_puff[a];
        const float R_b = pf[0] * r_hit + U * (pf[1] + pf[2] * k);
        const float cn = t.cluster ? cluster_n[i] : 1.f;
        float burst_R = 0.f, burst_T = 0.f; // the burst now (radius, heat) for its glow
        bool burst_alive = false;
        if (!hs.orphan && R_b > 0.f) {
            const float life = element_life_ms(pf[3] + pf[4] * k, element_hash(e.sequence, 3), t.stagger, floor_ms, &fl);
            const bool dust = a == hl::an_asteroid;
            if (age < life) {
                const float x = age / life;
                burst_R = puff_radius(a, R_b, age, life);
                burst_T = pf[5] * (1.f - x);
                burst_alive = true;
                if (t.burst && fireball_cap) {
                    const float mix = pf[9] > 0.f && saturated ? pf[9] * (1.f - x) : 0.f;
                    const float tint4[4] = {dust ? hl::dust_colour[0] : tint[0], dust ? hl::dust_colour[1] : tint[1],
                                            dust ? hl::dust_colour[2] : tint[2], dust ? 1.f : mix};
                    const float sb = 1.f / (1.f + (cn - 1.f) * (1.f / hl::cluster_n0_burst));
                    // the white-hot core (fire-material.md 2.3): class_fire_core (1 - x / fire_core_x)^2 x s(N)^2
                    const float gx = 1.f - x / hl::fire_core_x;
                    const float gcore = t.fire_core && x < hl::fire_core_x ? hl::class_fire_core[a] * gx * gx * sb * sb : 0.f;
                    // round 2: a fire class's weight 1 + (1 - s(N)), the cluster's thinning (hull::fire_cluster_thr)
                    const float fire_w = !t.fire ? 0.f : hl::class_fire[a] >= 1.f ? 2.f - sb : hl::class_fire[a];
                    if (puff_box(c_view, r_hit, burst_R, burst_T, pf[6] * gain * sb, x, core::look::fireball_advect * x,
                                 e.sequence, tint4, age_ms, own, e.owner, false, fire_w, gcore, age))
                        n.floored += fl;
                }
            }
            const float* sm = hl::class_smoke[a];
            if (t.smoke && smoke_cap && sm[0] > 0.f) {
                const float y = (age - hl::smoke_t0 * life) / (hl::smoke_span * life);
                if (y > 0.f && y < 1.f) {
                    const float sv = sm[1] * (y < hl::smoke_rise ? y / hl::smoke_rise : 1.f) * (1.f - y) * (1.f / (1.f - hl::smoke_rise)) * gain;
                    const float tint4[4] = {sv * hl::scorch_colour[0], sv * hl::scorch_colour[1], sv * hl::scorch_colour[2], -1.f};
                    puff_box(c_view, r_hit, sm[0] * R_b, 0.f, 0.f, y, core::look::fireball_advect * (age / life), e.sequence,
                             tint4, age_ms, own, e.owner, true);
                }
            }
        }
        // Surface ring: the expanding sphere shell cut by the lane (no normal), drawn as the shock front (weapon_ring_ps.hlsl:
        // the white-hot edge, the redder trailing band, the noise's arcs and the wash; stretched along the heading).
        const float* rg = hl::class_ring[a];
        float ring_r_now = 0.f, ring_e_now = 0.f, ring_screens = 0.f; // the drawn ring for the shimmer rect's band
        if (t.ring && ring_cap && !hs.orphan && rg[0] > 0.f) {
            const float life = element_life_ms(rg[2], element_hash(e.sequence, 6), t.stagger, floor_ms, &fl);
            if (age < life) {
                const float x = age / life;
                const float rr = rg[0] * r_hit * (1.f - core::exp_neg(-x / hl::ring_rise)), w = rg[1] * r_hit;
                // the shell's profile at the box edge (ring_front_window w), the stretched front inside its capsule
                const float stretch = hs.has_axis ? hl::ring_stretch : 1.f, R0 = rr + hl::ring_front_window * w;
                const float R = stretch * R0;
                float rect[4];
                bool full = false;
                if (c_view[2] + R > f.near_z && capsule_rect(f, c_view, a_view, stretch, R0, rect, &full)) {
                    if (nr < ring_cap) {
                        float colour[3];
                        core::heat_colour(hl::ring_lead_t, colour);
                        const float e0 = rg[3] * hl::ring_t * (1.f - x) * (1.f - x) * gain;
                        for (unsigned q = 0; q < 3; ++q) colour[q] *= e0;
                        // r, 1 / w, the noise's seed (-1: none), the wash's gain (0: none); light.w the wash's
                        // exponent rate log2(e) / (ring_wash_fall r) (the program reads no box radius)
                        const float seed = hl::ring_seed_scale * unit_of(element_hash(e.sequence, hl::ring_seed_element));
                        const float shape[4] = {rr, 1.f / w, t.ring_noise ? seed : -1.f, t.ring_wash ? hl::ring_wash_gain : 0.f};
                        // M = R - (1 - 1 / stretch) (R a) a^T: the owner's axes (the view's without its frame) with the
                        // stretch along the heading folded in (the noise rides the hull, the front is longer along it).
                        if (!s->frame_state) s->frame_state = owner_frame(s->data, f.cam, s->centre, s->frame) ? 1 : 2;
                        float m[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
                        if (s->frame_state == 1)
                            for (unsigned q = 0; q < 9; ++q) m[q] = s->frame[q];
                        if (hs.has_axis) {
                            const float kq = 1.f - 1.f / hl::ring_stretch;
                            for (unsigned row = 0; row < 3; ++row) {
                                const float ra = dot3(m + 3 * row, a_view) * kq;
                                for (unsigned q = 0; q < 3; ++q) m[3 * row + q] -= ra * a_view[q];
                            }
                        }
                        shape_quad(f, c_view, R, rect, full, colour, shape, m, out.rings + 4 * nr);
                        const float wash_rate = 1.4426950f / (hl::ring_wash_fall * (rr > 1e-6f ? rr : 1e-6f));
                        for (unsigned k = 0; k < 4; ++k) out.rings[4 * nr + k].light[3] = wash_rate;
                        ring_meta[nr] = BoxMeta{age_ms, e.owner, rect_screens(rect) * hl::budget_weight_ring, std::uint8_t(own), 0};
                        ++nr;
                        ++n.rings;
                        n.floored += fl;
                        // the refraction band rides a drawn ring whose rect stays within the shimmer's cap (a capped,
                        // culled or eye-filling ring does not refract)
                        if (t.ring_refraction && !full && rect_screens(rect) <= x3m::engine_shimmer::weapon_ring_max_screens) {
                            ring_r_now = rr;
                            ring_e_now = hl::ring_refract * (1.f - x) * (1.f - x);
                            ring_screens = rect_screens(rect);
                        }
                    } else
                        ++n.capped;
                }
            }
        }
        // Lightning (ion), the first of its two passes: the strikes drawn at this age (the flash, the afterglow of the
        // strike before it: one job each), their visible segments counted (the second pass after the records writes
        // them, whole hits only, oldest cut first when the cap binds).
        IonPhase ion_ph;
        if (a == hl::an_ion) ion_ph = ion_phase(e.sequence, float(life_age), floor_ms);
        if (t.filament && filament_cap && !hs.orphan && a == hl::an_ion && ion_ph.lit) {
            if (!s->frame_state) s->frame_state = owner_frame(s->data, f.cam, s->centre, s->frame) ? 1 : 2;
            const float px_x = 0.5f * f.width * f.cam.m00;
            const float r_px = c_view[2] > f.near_z ? r_hit * px_x / c_view[2] : 0.f;
            const float rows = f.height * (1.f / 1440.f);
            const float floor_main = hl::ion_core_px * rows > 1.f ? hl::ion_core_px * rows : 1.f;
            const float floor_branch = hl::ion_branch_px * rows > 1.f ? hl::ion_branch_px * rows : 1.f;
            const float w_main = hl::ion_core_w * r_px, w_branch = 0.5f * w_main;
            const float full_main = w_main > floor_main ? w_main : floor_main;
            const float full_branch = w_branch > floor_branch ? w_branch : floor_branch;
            // the strikes' sphere (their reach and the full-width corona's pixels) off screen: nothing to build (the
            // performance pass: no strike, no points)
            float rect[4];
            bool full = false;
            const float margin = c_view[2] > f.near_z ? (hl::ion_corona_x * 0.5f * full_main + 1.f) * c_view[2] / px_x : 0.f;
            if (s->frame_state == 1 && c_view[2] > f.near_z && sphere_rect(f, c_view, hl::ion_reach_r * r_hit + margin, rect, &full)) {
                // Each segment's cull is the sphere about the contact through its farther end (the vertex program
                // may turn the plane onto the lane's normal, so the stage's projection does not bound it): a point
                // d from the contact, |d| <= rho, projects within px_x rho (1 + |c_x| / c_z) / (c_z - rho) of it
                // (rho the L1 norm of its plane coordinates x r_hit: no root).
                const Px cp = to_px(f, sc, c_view);
                const float iz = rcp_nr(c_view[2]);
                const float ax = c_view[0] * iz, ay = c_view[1] * iz;
                const float kx = px_x * r_hit * (1.f + (ax < 0.f ? -ax : ax));
                const float ky = 0.5f * f.height * f.cam.m11 * r_hit * (1.f + (ay < 0.f ? -ay : ay));
                float t1[3], t2[3];
                ion_tangents(e.pos, s->frame, t1, t2);
                unsigned hit_jobs = 0;
                for (unsigned part = 0; part < 2 && n_ion_jobs < ion_job_capacity; ++part) {
                    const bool is_flash = part == 0;
                    if (is_flash ? !ion_ph.flash : !ion_ph.after) continue;
                    const float wf = is_flash ? ion_ph.w : ion_ph.after_w, ef = is_flash ? ion_ph.e : ion_ph.after_e;
                    if (!(ef > 0.f)) continue;
                    const float hw_main = 0.5f * (wf * full_main > 1.f ? wf * full_main : 1.f);
                    const float hw_branch = 0.5f * (wf * full_branch > 1.f ? wf * full_branch : 1.f);
                    const unsigned strike = ion_cycle_strike(ion_cycle, is_flash ? ion_ph.strike : ion_ph.after_strike);
                    IonJob& j = ion_jobs[n_ion_jobs];
                    const IonStrike& st = ion_strike_cached(e.sequence, strike);
                    unsigned count = 0;
                    for (unsigned w = 0; w < IonJob::mask_words; ++w) j.mask[w] = 0;
                    for (unsigned l = 0; l < st.lines; ++l) {
                        const IonLine& ln = st.line[l];
                        const float q = hl::ion_corona_x * (ln.branch ? hw_branch : hw_main) + 1.f;
                        for (unsigned i = ln.first; i + 1 < unsigned(ln.first) + ln.points; ++i) {
                            const float* pa = st.p[i];
                            const float* pb = st.p[i + 1];
                            const float ra = (pa[0] < 0.f ? -pa[0] : pa[0]) + (pa[1] < 0.f ? -pa[1] : pa[1]);
                            const float rb = (pb[0] < 0.f ? -pb[0] : pb[0]) + (pb[1] < 0.f ? -pb[1] : pb[1]);
                            const float rho = ra > rb ? ra : rb;
                            const float zmin = c_view[2] - rho * r_hit;
                            if (zmin > f.near_z) {
                                const float izm = rcp_nr(zmin);
                                const float hx = kx * rho * izm + q, hy = ky * rho * izm + q;
                                if (cp.x + hx < 0.f || cp.x - hx > f.width || cp.y + hy < 0.f || cp.y - hy > f.height) continue;
                            }
                            j.mask[i >> 5] |= 1u << (i & 31u);
                            ++count;
                        }
                    }
                    if (!count) continue;
                    for (unsigned q = 0; q < 3; ++q) {
                        j.c[q] = c_view[q];
                        j.t1[q] = t1[q] * r_hit;
                        j.t2[q] = t2[q] * r_hit;
                    }
                    j.r_px = r_px;
                    j.hw_main = hw_main;
                    j.hw_branch = hw_branch;
                    j.e_main = hl::ion_channel_e * ef * gain;
                    j.e_branch = j.e_main * hl::ion_branch_e;
                    j.tol = hl::ion_depth_r * r_hit;
                    j.age = life_age; // a held beam's strike age: its current strikes are not the oldest
                    j.sequence = e.sequence;
                    j.segments = std::uint16_t(count);
                    j.strike = std::uint8_t(strike);
                    j.own = std::uint8_t(own);
                    j.kept = 1;
                    j.part = std::uint8_t(hit_jobs ? 1 : 0);
                    ++n_ion_jobs;
                    ++hit_jobs;
                }
                if (hit_jobs) {
                    ++n.filaments;
                    n.floored += floor_ms > float(hl::ion_strike_ms);
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
            // its charge (screens): the square's projected area, or the ring's rect when it carries the ring
            const float side = sr.size * f.cam.m00 * f.width / c_view[2], square = side * side / (4.f * f.width * f.height);
            const float drawn = ring_screens > square ? ring_screens : square;
            shimmer_screens[shimmer_cands - 1] = drawn < 1.f ? drawn : 1.f;
            sr.strength = 1.f - age / float(hl::shimmer_ms);
            sr.ring_r = ring_r_now; // the ring's refraction band (0: none)
            sr.ring_strength = ring_e_now;
            sr.ring_stretch = hs.has_axis ? hl::ring_stretch : 1.f;
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
            const bool ion = a == hl::an_ion, rock = a == hl::an_asteroid;
            const std::uint32_t ion_packed = pack_colour(rock ? hl::dust_colour : hl::ion_colour, 0.f);
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
                    const std::uint32_t packed = ion || rock ? ion_packed : heat_packed(ss.t0 * fr2);
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
        // Glow and ember (the owner live): the burst's glow (section 3: glow_r_x R(t), glow_e_x E_b, the burst's heat and
        // life, the cluster's saturation), repair's and tug's cool glow (class_glow), the ember.
        if (!hs.orphan) {
            if (t.glow && burst_alive && burst_R > 0.f && a != hl::an_asteroid) {
                float colour[3];
                core::heat_colour(burst_T, colour);
                float sg = 1.f / (1.f + (cn - 1.f) * (1.f / hl::cluster_n0_glow));
                // round 2: a fire class's glow under a dense cluster lower still (the gaps between the filaments dark)
                if (t.fire && hl::class_fire[a] > 0.f && cn > hl::cluster_glow_fire_n) sg *= hl::cluster_glow_fire_n / cn;
                const float e0 = hl::glow_e_x * pf[6] * burst_T * sg * gain;
                for (unsigned q = 0; q < 3; ++q) colour[q] *= e0;
                float Rg = hl::glow_r_x * burst_R;
                if (rg[0] > 0.f && t.glow_to_ring) { // a ring class: out to the ring's current radius while it lives (core.h, ember_lobe_amp)
                    bool rfl = false;
                    const float rlife = element_life_ms(rg[2], element_hash(e.sequence, 6), t.stagger, floor_ms, &rfl);
                    if (age < rlife) {
                        const float xr = age / rlife;
                        // the ring's drawn reach (its front's window past r(t)): the glow still lit under its band
                        const float rr = rg[0] * r_hit * (1.f - core::exp_neg(-xr / hl::ring_rise)) +
                                         hl::ring_front_window * rg[1] * r_hit;
                        if (rr > Rg) Rg += (rr - Rg) * (1.f - xr);
                    }
                }
                if (glow_box(c_view, Rg, colour, age_ms, own, e.owner, core::look::budget_weight_glow)) ++n.glows;
            }
            // the ion's cold light on the hull (core::hull ion_glow_*): ion_colour, never the heat ramp; ion_glow_r x
            // the latest strike's extent, flickering with the strikes (its strike is in the cache: the lightning's)
            if (t.glow && a == hl::an_ion && ion_ph.glow > 0.f) {
                const IonStrike& st = ion_strike_cached(e.sequence, ion_cycle_strike(ion_cycle, ion_ph.latest));
                float colour[3];
                for (unsigned q = 0; q < 3; ++q) colour[q] = hl::ion_colour[q] * hl::ion_glow_e * ion_ph.glow * gain;
                if (glow_box(c_view, hl::ion_glow_r * st.extent * r_hit, colour, age_ms, own, e.owner,
                             core::look::budget_weight_glow))
                    ++n.glows;
            }
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
                    for (unsigned q = 0; q < 3; ++q) colour[q] = hl::cool_colour[q] * e0 * fade;
                } else if (is_glow) {
                    core::heat_colour(gw[2], colour);
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= gw[3] * gw[2] * fade;
                } else {
                    const float T = em[3] * fade;
                    core::heat_colour(T, colour);
                    // round 2 (fire-material.md section 12): a fire class's ember (the smooth lobed glow under the
                    // filaments) saturates with the cluster as the burst's glow does (the 20 hits' embers summed to a tan
                    // puffball)
                    const float se = t.fire && hl::class_fire[a] > 0.f
                                         ? 1.f / (1.f + (cn - 1.f) * (1.f / hl::cluster_n0_glow)) *
                                               (cn > hl::cluster_glow_fire_n ? hl::cluster_glow_fire_n / cn : 1.f)
                                         : 1.f;
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= em[4] * T * se;
                }
                for (unsigned q = 0; q < 3; ++q) colour[q] *= gain;
                // a burst class's ember: the burst's lobed silhouette (its rotation and lobe seed), the body's remnant
                const bool lobed = !is_glow && R_b > 0.f && hl::ember_lobe_amp > 0.f;
                float lobe[2] = {0.f, -1.f};
                if (lobed) {
                    lobe[0] = 6.2831853f * unit_of(element_hash(e.sequence, hl::burst_rot_element));
                    lobe[1] = 64.f * unit_of(element_hash(e.sequence, hl::burst_lobe_element));
                }
                if (glow_box(c_view, R, colour, age_ms, own, e.owner,
                             lobed ? core::look::budget_weight_glow_lobed : core::look::budget_weight_glow, lobed ? lobe : nullptr)) {
                    if (is_glow)
                        ++n.glows;
                    else
                        ++n.embers;
                    n.floored += fl;
                }
            }
        }
    }
    // The stamp (core::ring_stamp_hidden) of the kept owners: one with a hull hit drawn and in view when in reach (its
    // reach sphere, or a record's spark sphere: the cull's test), a shield-only one in view by owner_in_view (kept for
    // its track only; the shield stage judges its drawing). A record dropped by the budget still counts as drawn.
    if (visible)
        for (unsigned k = 0; k < keep; ++k) {
            const Seen& sn = seen[k];
            if (sn.status != OwnerStatus::ok) continue;
            if (cand[k].hull ? !sn.out || sn.reached : owner_in_view(sn.data, f.cam, f.near_z, visible->far_z)) {
                visible->add_in_view(sn.owner);
                if (cand[k].hull) visible->add(sn.owner);
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
                              : a == hl::an_asteroid ? (hl::smoke_t0 + hl::smoke_span) * hl::class_puff[a][3] +
                                                           float(hl::spot_fall_ms)
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
        // The beam is light (section 4.5, 2026-11-07): its held spot's halo and glow white mixed beam_glow_mix towards
        // the weapon colour, the core beam_core_mix, where the weapon has a colour (beam_coloured); everything heated
        // stays on the heat ramp.
        const bool beam_lit = a == hl::an_beam && t.beam_colour && beam_coloured(e);
        float beam_c[3];
        for (unsigned q = 0; q < 3; ++q) beam_c[q] = 1.f + (tint[q] - 1.f) * hl::beam_glow_mix;
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
            if (t.flash && sp.flash && a == hl::an_beam) { // the pulse start's star
                const float life = element_life_ms(fr[2], element_hash(sp.seed, 0), t.stagger, sp.floor_ms, &fl);
                float halo[3];
                core::heat_colour(fr[3], halo);
                if (float(age_first) < life &&
                    emit_star(c_view, r_hit, fr[0], fr[1], sp.seed, halo,
                              core::look::core_e * (1.f - float(age_first) / life) * gain)) {
                    ++n.flashes;
                    ++n.spot_flashes;
                }
            }
            const float env = 1.f - fall_age / float(hl::spot_fall_ms);
            if (t.flash && t.spot_core && env > 0.f && a == hl::an_beam) {
                float halo[3];
                if (beam_lit)
                    for (unsigned q = 0; q < 3; ++q) halo[q] = beam_c[q];
                else
                    core::heat_colour(1.f, halo);
                if (emit_flash(c_view, r_hit, hl::spot_core, hl::spot_halo, halo, core::look::core_e * env * gain,
                               beam_lit ? hl::beam_core_mix / hl::beam_glow_mix : 0.f))
                    ++n.flashes;
            }
            // The boiling puff: grown from the spot's birth, its heat re-lit at each tick (never added), eroding over its
            // fall (on rock the unlit dust); its glow (the burst's law, not on rock) and its smoke over the fall; its noise
            // drifts outward with the spot's age (one phase from its birth, no reset).
            const float* pf = hl::class_puff[a];
            const float L = pf[3];
            const float T = pf[5] * (1.f - fall_age / L);
            const bool dust = a == hl::an_asteroid;
            const float R = pf[0] * r_hit * (1.f - core::exp_neg(-float(age_first) / (pf[7] * L)));
            if (t.burst && fireball_cap && T > 0.f) {
                const float tint4[4] = {dust ? hl::dust_colour[0] : 0.f, dust ? hl::dust_colour[1] : 0.f,
                                        dust ? hl::dust_colour[2] : 0.f, dust ? 1.f : 0.f};
                // the fire material without the core (a held spot's core would be a lamp for the whole hold)
                puff_box(c_view, r_hit, R, T, pf[6] * gain, fall_age / L,
                         core::look::fireball_advect * float(age_first) * 1e-3f, sp.seed, tint4, rel, own, e.owner,
                         false, t.fire ? hl::class_fire[a] : 0.f, 0.f, float(age_first));
            }
            if (t.glow && !dust && T > 0.f && R > 0.f) {
                float colour[3];
                core::heat_colour(T, colour);
                if (beam_lit) { // the weapon colour at the heat glow's mean radiance
                    const float m = (colour[0] + colour[1] + colour[2]) / (beam_c[0] + beam_c[1] + beam_c[2]);
                    for (unsigned q = 0; q < 3; ++q) colour[q] = beam_c[q] * m;
                }
                for (unsigned q = 0; q < 3; ++q) colour[q] *= hl::glow_e_x * pf[6] * T * gain;
                if (glow_box(c_view, hl::glow_r_x * R, colour, rel, own, e.owner, core::look::budget_weight_glow)) ++n.glows;
            }
            const float* sm = hl::class_smoke[a];
            const float y = (fall_age - hl::smoke_t0 * L) / (hl::smoke_span * L);
            if (t.smoke && smoke_cap && sm[0] > 0.f && y > 0.f && y < 1.f) {
                const float sv = sm[1] * (y < hl::smoke_rise ? y / hl::smoke_rise : 1.f) * (1.f - y) * (1.f / (1.f - hl::smoke_rise)) * gain;
                const float tint4[4] = {sv * hl::scorch_colour[0], sv * hl::scorch_colour[1], sv * hl::scorch_colour[2], -1.f};
                puff_box(c_view, r_hit, sm[0] * pf[0] * r_hit, 0.f, 0.f, y,
                         core::look::fireball_advect * float(age_first) * 1e-3f, sp.seed, tint4, rel, own, e.owner,
                         true);
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
                                        dust ? pack_colour(hl::dust_colour, 0.f) : heat_packed(ss.t0 * fr2),
                                        out.sprites + 4 * nq, sf.len_x);
                        ++nq;
                        ++n.sparks;
                        --sparks_left;
                        ++drawn;
                    }
                }
            }
        }
        // Repair's pulse start: a small cool star (class_flash, no halo; the tug takes none).
        if (a == hl::an_repair && t.flash && sp.flash) {
            const float* fr = hl::class_flash[a];
            const float life = element_life_ms(fr[2], element_hash(sp.seed, 0), t.stagger, sp.floor_ms, &fl);
            if (float(age_first) < life &&
                emit_star(c_view, r_hit, fr[0], fr[1], sp.seed, hl::cool_colour,
                          core::look::core_e * (1.f - float(age_first) / life) * gain)) {
                ++n.flashes;
                ++n.spot_flashes;
            }
        }
        // The cool glow of repair and tug (class_glow: held, then its fall; the beam's is its puff's above).
        const float* gw = hl::class_glow[a];
        const float genv = gw[1] > 0.f ? 1.f - fall_age / gw[1] : 0.f;
        if (t.glow && gw[0] > 0.f && genv > 0.f) {
            float colour[3];
            for (unsigned q = 0; q < 3; ++q) colour[q] = hl::cool_colour[q] * gw[3] * genv * gain;
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
                    for (unsigned q = 0; q < 3; ++q) colour[q] = hl::cool_colour[q] * em[4] * fade * gain;
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
    // (C2) The lightning's second pass: the jobs' segments written, whole hits only (a hit's flash and afterglow jobs
    // together). Past filament_cap segments the newest hits are kept while they fit and the rest cut whole, oldest first (review 2026-11-07: never a channel cut
    // mid-way; n.capped counts the hits cut). Each segment charged its quad's screens: (its plane length x r_px + 2 q) x
    // 2 q (q the corona's half width + 1 px), the plane's foreshortening not known here (an upper bound), x
    // budget_weight_filament; per channel (a main line with its branches) for the shed in (F).
    bool ion_sorted = false;
    auto sort_by_age = [&] { // the newest first, ties by job index (a stable choice); once a frame, only when needed
        if (ion_sorted) return;
        for (unsigned k = 0; k < n_ion_jobs; ++k) ion_order[k] = std::uint16_t(k);
        std::sort(ion_order, ion_order + n_ion_jobs, [](std::uint16_t x, std::uint16_t y) {
            return ion_jobs[x].age != ion_jobs[y].age ? ion_jobs[x].age < ion_jobs[y].age : x < y;
        });
        ion_sorted = true;
    };
    {
        unsigned total = 0;
        for (unsigned k = 0; k < n_ion_jobs; ++k) total += ion_jobs[k].segments;
        if (total > filament_cap) {
            sort_by_age();
            unsigned used = 0;
            bool cut = false;
            for (unsigned k = 0; k < n_ion_jobs; ++k) {
                IonJob& j = ion_jobs[ion_order[k]];
                if (j.part) continue; // decided with its hit's first job (adjacent: the same age, the next index)
                IonJob* second = k + 1 < n_ion_jobs && ion_jobs[ion_order[k + 1]].part ? &ion_jobs[ion_order[k + 1]] : nullptr;
                const unsigned segs = j.segments + (second ? second->segments : 0u);
                cut = cut || used + segs > filament_cap;
                if (cut) {
                    j.kept = 0;
                    if (second) second->kept = 0;
                    ++n.capped;
                    --n.filaments;
                } else
                    used += segs;
            }
        }
        const float inv_screen = 1.f / (f.width * f.height);
        for (unsigned k = 0; k < n_ion_jobs; ++k) {
            IonJob& j = ion_jobs[k];
            if (!j.kept) continue;
            const IonStrike& st = ion_strike_cached(j.sequence, j.strike);
            j.first = std::uint16_t(nf);
            j.channels = 0;
            const __m128 va_m = _mm_setr_ps(j.c[0], j.c[1], j.c[2], j.e_main);
            const __m128 va_b = _mm_setr_ps(j.c[0], j.c[1], j.c[2], j.e_branch);
            const __m128 cm_m = _mm_setr_ps(j.t1[0], j.t1[1], j.t1[2], -j.hw_main);
            const __m128 cp_m = _mm_setr_ps(j.t1[0], j.t1[1], j.t1[2], j.hw_main);
            const __m128 cm_b = _mm_setr_ps(j.t1[0], j.t1[1], j.t1[2], -j.hw_branch);
            const __m128 cp_b = _mm_setr_ps(j.t1[0], j.t1[1], j.t1[2], j.hw_branch);
            const __m128 dm = _mm_setr_ps(j.t2[0], j.t2[1], j.t2[2], -j.tol);
            const __m128 dp = _mm_setr_ps(j.t2[0], j.t2[1], j.t2[2], j.tol);
            const float q_m = hl::ion_corona_x * j.hw_main + 1.f, q_b = hl::ion_corona_x * j.hw_branch + 1.f;
            const float w_m = 2.f * q_m * inv_screen * hl::budget_weight_filament;
            const float w_b = 2.f * q_b * inv_screen * hl::budget_weight_filament;
            int ch = -1;
            for (unsigned l = 0; l < st.lines; ++l) {
                const IonLine& ln = st.line[l];
                const bool b = ln.branch != 0;
                if (!b && ch + 1 < int(hl::ion_channels_hi)) {
                    ++ch;
                    j.chan_area[ch] = 0.f;
                }
                if (ch < 0) continue;
                for (unsigned i = ln.first; i + 1 < unsigned(ln.first) + ln.points; ++i) {
                    if (!(j.mask[i >> 5] & (1u << (i & 31u)))) continue;
                    const float* pa = st.p[i];
                    const float* pb = st.p[i + 1];
                    ion_emit(out.filaments + 4 * nf, b ? va_b : va_m, _mm_setr_ps(pa[0], pa[1], pb[0], pb[1]),
                             b ? cm_b : cm_m, b ? cp_b : cp_m, dm, dp);
                    j.chan_area[ch] += (st.len[i] * j.r_px + 2.f * (b ? q_b : q_m)) * (b ? w_b : w_m);
                    ++nf;
                }
                j.chan_end[ch] = std::uint16_t(nf - j.first);
            }
            j.channels = std::uint8_t(ch + 1);
            j.keep_channels = j.channels;
            n.ion_segments += nf - j.first;
        }
    }
    // (F) The shared budget (look 2.11 as amended by hull-burst-look.md section 6): past the screens the patches left,
    // (0) the shimmer rects of other owners, (1) every burst's third octave, (2) other owners' smoke, (3) their
    // scorches, (4) their rings and filament glows, (5) their glows and embers, then their bursts past two per owner
    // (oldest first, O(boxes x shed), only while over budget), last other owners' flashes. Streaks, sparks, chips, spot
    // cores and anything of the own ship are never shed.
    float area = 0.f, burst_area = 0.f;
    for (unsigned g = 0; g < nm; ++g) area += smoke_meta[g].area;
    for (unsigned g = 0; g < ng; ++g) area += glow_meta[g].area;
    for (unsigned g = 0; g < nb; ++g) burst_area += burst_meta[g].area;
    for (unsigned g = 0; g < nr; ++g) area += ring_meta[g].area;
    for (unsigned k = 0; k < n_ion_jobs; ++k)
        if (ion_jobs[k].kept)
            for (unsigned c = 0; c < ion_jobs[k].channels; ++c) area += ion_jobs[k].chan_area[c];
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
        area += shimmer_screens[best] * hl::budget_weight_shimmer; // charged, never shed past this point
    }
    for (unsigned c = 0; c < shimmer_cands; ++c) n.shimmer_shed += !taken[c];
    n.shimmer = out.shimmer_count;
    // (1) every burst drops its third octave (the frame's octaves 2): entered when the charged screens pass the budget,
    // held at least fireball_octave_hold frames and left only under fireball_octave_return of the budget (judged on the
    // full charge, so leaving never re-enters on the next frame by itself).
    // (0b) every burst drops its fourth octave first (the frame's octaves 3) past the budget
    if (area > f.budget_left && n.octaves > 3u) {
        for (unsigned g = 0; g < nb; ++g)
            if (burst_fine[g] > 1.f) {
                const float fx = (core::look::budget_weight_fire - core::look::budget_weight_fireball) * burst_fire[g];
                const float now = burst_meta[g].area * (octave_weight(1.f) + fx) / (octave_weight(burst_fine[g]) + fx);
                area -= burst_meta[g].area - now;
                burst_area -= burst_meta[g].area - now;
                burst_meta[g].area = now;
                burst_fine[g] = 1.f;
            }
        n.octaves = 3;
    }
    // (0c) the tongues of other owners' bursts, farthest first (fire-material.md section 7; their boxes draw the small
    // size's material, LOD 0): entered past the budget, held at least fireball_octave_hold frames and left only under
    // fireball_octave_return of it; while held, taken until the charge is under that return (the own owner's keep them)
    {
        bool any = false;
        for (unsigned g = 0; g < nb && !any; ++g) any = burst_fire[g] > 0.f && !burst_meta[g].own;
        if (state.fire_coarse) {
            if (state.fire_frames >= core::look::fireball_octave_hold && area < core::look::fireball_octave_return * f.budget_left)
                state.fire_coarse = false;
            else if (state.fire_frames < 255)
                ++state.fire_frames;
        } else if (area > f.budget_left && any) {
            state.fire_coarse = true;
            state.fire_frames = 1;
        }
        constexpr float extra = core::look::budget_weight_fire - core::look::budget_weight_fireball;
        while (state.fire_coarse && area > core::look::fireball_octave_return * f.budget_left) {
            unsigned sel = nb;
            for (unsigned g = 0; g < nb; ++g)
                if (burst_fire[g] > 0.f && !burst_meta[g].own && (sel == nb || burst_depth[g] > burst_depth[sel])) sel = g;
            if (sel == nb) break;
            const float w = octave_weight(burst_fine[sel]) + extra * burst_fire[sel];
            const float cut = burst_meta[sel].area * extra * burst_fire[sel] / w;
            area -= cut;
            burst_area -= cut;
            burst_meta[sel].area -= cut;
            burst_fire[sel] = 0.f;
            for (unsigned k = 0; k < 4; ++k) out.fireballs[4 * sel + k].core[2] =
                    -(out.fireballs[4 * sel + k].core[2] < 1.f ? out.fireballs[4 * sel + k].core[2] : 1.f); // the law kept, no tongues
            ++n.fire_shed;
        }
        // the tongues' own fence (hull::fire_fence, round 2): past it in screens of the tongues' charge, other owners'
        // bursts drop the term farthest first until under fireball_octave_return of it; held fireball_octave_hold frames
        // (a cluster: more than cluster_glow_fire_n other owners' bursts with tongues; the nearest that many keep theirs)
        float tongues = 0.f;
        unsigned others = 0;
        for (unsigned g = 0; g < nb; ++g)
            if (burst_fire[g] > 0.f) {
                tongues += burst_meta[g].area * extra * burst_fire[g] / (octave_weight(burst_fine[g]) + extra * burst_fire[g]);
                others += !burst_meta[g].own;
            }
        constexpr unsigned keep = unsigned(hl::cluster_glow_fire_n);
        if (state.fire_fenced) {
            if (state.fence_frames >= core::look::fireball_octave_hold &&
                tongues < core::look::fireball_octave_return * hl::fire_fence)
                state.fire_fenced = false;
            else if (state.fence_frames < 255)
                ++state.fence_frames;
        } else if (tongues > hl::fire_fence && others > keep) {
            state.fire_fenced = true;
            state.fence_frames = 1;
        }
        while (state.fire_fenced && others > keep && tongues > core::look::fireball_octave_return * hl::fire_fence) {
            unsigned sel = nb;
            for (unsigned g = 0; g < nb; ++g)
                if (burst_fire[g] > 0.f && !burst_meta[g].own && (sel == nb || burst_depth[g] > burst_depth[sel])) sel = g;
            if (sel == nb) break;
            const float w = octave_weight(burst_fine[sel]) + extra * burst_fire[sel];
            const float cut = burst_meta[sel].area * extra * burst_fire[sel] / w;
            tongues -= cut;
            --others;
            area -= cut;
            burst_area -= cut;
            burst_meta[sel].area -= cut;
            burst_fire[sel] = 0.f;
            for (unsigned k = 0; k < 4; ++k) out.fireballs[4 * sel + k].core[2] =
                    -(out.fireballs[4 * sel + k].core[2] < 1.f ? out.fireballs[4 * sel + k].core[2] : 1.f); // the law kept, no tongues
            ++n.fire_shed;
        }
    }
    if (state.octaves_coarse) {
        if (state.coarse_frames >= core::look::fireball_octave_hold && area < core::look::fireball_octave_return * f.budget_left)
            state.octaves_coarse = false;
        else if (state.coarse_frames < 255)
            ++state.coarse_frames;
    } else if (area > f.budget_left && nb) {
        state.octaves_coarse = true;
        state.coarse_frames = 1;
    }
    if (state.octaves_coarse) {
        constexpr float w2 = core::look::budget_weight_fireball_coarse;
        for (unsigned g = 0; g < nb; ++g)
            if (burst_meta[g].rank) {
                const float we = octave_weight(burst_fine[g]);
                const float fx = (core::look::budget_weight_fire - core::look::budget_weight_fireball) * burst_fire[g];
                const float cut = burst_meta[g].area * (we - w2) / (we + fx);
                area -= cut;
                burst_area -= cut;
                burst_meta[g].area -= cut;
            }
        n.octaves = 2;
    }
    n.smokes_shed += shed_oldest(out.smokes, &nm, smoke_meta, &area, f.budget_left, 0);
    n.scorches_shed += shed_oldest(out.scorches, &ns, scorch_meta, &area, f.budget_left, 0);
    n.rings_shed += shed_oldest(out.rings, &nr, ring_meta, &area, f.budget_left, 0);
    // The lightning (step 4 with the rings; review 2026-11-07: one pass, whole channels): other owners' hits oldest first,
    // each hit's channels from its last (a channel goes with its branches), while over budget; then the kept segments
    // move down in one pass. n.filaments_shed counts the segments shed.
    if (area > f.budget_left && nf) {
        sort_by_age();
        bool any = false;
        for (unsigned k = n_ion_jobs; k-- > 0 && area > f.budget_left;) {
            IonJob& j = ion_jobs[ion_order[k]];
            if (!j.kept || j.own) continue;
            while (j.keep_channels && area > f.budget_left) {
                area -= j.chan_area[--j.keep_channels];
                any = true;
            }
        }
        if (any) {
            unsigned w = 0;
            for (unsigned k = 0; k < n_ion_jobs; ++k) {
                IonJob& j = ion_jobs[k];
                if (!j.kept) continue;
                const unsigned keep = j.keep_channels ? j.chan_end[j.keep_channels - 1] : 0u;
                const unsigned all = j.channels ? j.chan_end[j.channels - 1] : 0u;
                if (w != j.first && keep)
                    std::memmove(out.filaments + 4 * w, out.filaments + 4 * j.first, sizeof(*out.filaments) * 4 * keep);
                j.first = std::uint16_t(w);
                w += keep;
                n.filaments_shed += all - keep;
            }
            nf = w;
        }
    }
    n.shed += shed_oldest(out.glows, &ng, glow_meta, &area, f.budget_left, 0);
    if (area > f.budget_left && nb) { // each owner's bursts ranked newest first; past two per owner shed
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
    out.smoke_boxes = nm;
    n.smokes = nm;
    return n;
}
} // namespace x3m::weapon_effects::stage
