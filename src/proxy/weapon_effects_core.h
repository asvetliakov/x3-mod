#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Portable core of the weapon effects, step 1: the shield hit (docs/architecture/weapon-effects.md sections 2-5.1,
// docs/reverse-engineering/weapon-effect-spawn.md sections 1, 3 and 6). No Windows dependency: the DLL, the hook
// fixture, the D3D fixture and the host tests compile it, and tools/effects/generate_weapon_effects_lab.py reads the
// lab constants below by name (`name = value` at the start of a declarator).
//
// Two halves:
// - integer only (the impact handler's part, compiled into weapon_effects_hook.cpp with -mno-sse -mfpmath=387 and no
//   floating point at all): the Event record, the ring, the shield rule, the integer classifier, the engine's own
//   16.16 contact-point arithmetic and the handler body `impact()` / `tap()` over plain loads;
// - float (the stage and the table loader, SSE2): the shape thresholds on the body's elongation, the calibre, the
//   game-time age law and the look constants the lab tunes.
namespace x3m::weapon_effects::core {

// ---------------------------------------------------------------------------------------------- the record (3.1)
enum Role : std::uint8_t { role_none = 0, role_shield_hit = 1, role_hull_hit = 2, role_muzzle = 3, role_bolt = 4,
                           role_beam = 5, role_explosion = 6 };
inline const char* role_name(unsigned r) noexcept {
    static const char* const names[] = {"none", "shield_hit", "hull_hit", "muzzle", "bolt", "beam", "explosion"};
    return r < 7 ? names[r] : "?";
}
// style: the shape (low nibble) plus the burst bit; missile_hit is a fixed style of its own.
enum Shape : std::uint8_t { shape_tracer = 0, shape_slug = 1, shape_orb = 2, shape_wave = 3, shape_beam = 4,
                            shape_missile_hit = 5, shape_count = 6 };
constexpr std::uint8_t style_burst = 0x10, style_shape_mask = 0x0f;
inline const char* shape_name(unsigned s) noexcept {
    static const char* const names[shape_count] = {"tracer", "slug", "orb", "wave", "beam", "missile_hit"};
    return s < shape_count ? names[s] : "?";
}
enum Tier : std::uint8_t { tier_light = 0, tier_medium = 1, tier_heavy = 2 };
inline const char* tier_name(unsigned t) noexcept {
    return t == tier_light ? "light" : t == tier_medium ? "medium" : t == tier_heavy ? "heavy" : "?";
}
enum TableId : std::uint8_t { table_none = 0, table_bullets = 1, table_missiles = 2 };
inline const char* table_name(unsigned t) noexcept {
    return t == table_bullets ? "bullets" : t == table_missiles ? "missiles" : "none";
}
enum TintSource : std::uint8_t { tint_white = 0, tint_light = 1, tint_texture = 2, tint_override = 3, tint_missile = 4 };
inline const char* tint_source_name(unsigned t) noexcept {
    static const char* const names[] = {"white", "light", "texture", "override", "missile"};
    return t < 5 ? names[t] : "?";
}
// flag_forwarded: the engine's own spawn ran (the hook returned 0); flag_stage: the stage draws the record (a shield hit
// while the stage owned the previous frame). Coordinator decision 2026-11-06 (after Run235 Flight B): a stage-owned
// shield hit skips the engine's spawn (suppress_native_shield_hit), so it is not flag_forwarded.
// The former flag_owner_retired (set only on a record about to be released) gave its bit to flag_stage.
enum EventFlag : std::uint8_t { flag_absorbed = 1, flag_forwarded = 2, flag_nested = 4, flag_stage = 8,
                                flag_override_applied = 16, flag_calibre_override = 32, flag_logged = 64,
                                flag_shield_read = 128 };
// Whether a stage-owned shield hit also skips the engine's spawn (0x00413c40 not called: no native impact sprite, its
// light or its PROPAGATE children). true (coordinator decision 2026-11-06, superseding the user decision of the same
// day to keep the native sprite under the patch): in shields mode the patch alone answers a shield hit the stage owns;
// observe, a hull hit outside shields,hull mode, a dying hit object and every frame the stage does not own still
// forward. HookState::
// suppress_native carries it; the hook fixture drives both paths.
constexpr bool suppress_native_shield_hit = true;
// Step 2 (docs/architecture/weapon-look.md 3.1): in shields,hull mode a stage-owned hull hit (absorbed == 0, the
// hull-through-shield rows, a drained shield, a missile source) skips the engine's spawn too (the outer site: its
// PROPAGATE children and EEDF_LIGHT go with it); a dying hit object and every frame the stage did not own still forward.
// HookState::suppress_native_hull carries it.
constexpr bool suppress_native_hull_hit = true;
// A drained shield (user report after Run237 Flight D: shield hits drawn on ships without shields; x3m.log: post-hit
// shield fractions 0.0000 to 0.0005 on owners already at 0, the engine absorbing a weak bolt with the recharge
// trickle): a shield hit whose post-hit fraction (Event::shield_q, read, flag_shield_read) is below drained_shield_q
// (0.5 % of 65535) is a hull hit: the hook forwards the engine's spawn, records role_hull_hit and marks the record
// detail_drained; no patch is staged. Only the post-hit value is known, so a hit that drains a meaningful shield to
// zero in one blow is drained too. An unread shield keeps the engine's rule.
constexpr std::uint16_t drained_shield_q = 65535u * 5u / 1000u; // 327
// Event::detail bits (an impact record): the drained shield; the hull-hit variants (weapon-look.md 3.1): the repair and
// tug lasers by the curated override (the table's "hull" key: row_hull_repair / row_hull_tug), the asteroid by the hit
// object's class (17).
enum EventDetail : std::uint8_t { detail_drained = 1, detail_repair = 2, detail_tug = 4, detail_asteroid = 8 };
// Bits 4-7 of Event::detail: the hull-hit class (docs/architecture/hull-impact-classes.md section 2), decided at the
// hook from the live record (hull_class_from: flags, the running damage, the style's body shape) or the table's pin
// (row_hull_class, the override's `hull` key); missile sources, asteroid victims, repair and tug keep their own bits
// and styles. A record from before the classes (the nibble 0) is energy.
enum HullClass : std::uint8_t { class_energy = 0, class_ballistic = 1, class_plasma = 2, class_heavy = 3, class_ion = 4,
                                class_beam = 5, hull_class_count = 6 };
constexpr unsigned detail_class_shift = 4;
constexpr std::uint8_t detail_class_mask = 0xf0;
inline const char* hull_class_name(unsigned c) noexcept {
    static const char* const names[hull_class_count] = {"energy", "ballistic", "plasma", "heavy", "ion", "beam"};
    return c < hull_class_count ? names[c] : "?";
}
inline unsigned hull_class_of(const struct Event& e) noexcept;
// The hit object's size class for the patch (docs/architecture/weapon-effects.md 5.1.1): cell size and patch radius
// are fixed in world units per class.
enum PatchTier : std::uint8_t { patch_fighter = 0, patch_corvette = 1, patch_capital = 2, patch_tier_count = 3 };
inline const char* patch_tier_name(unsigned t) noexcept {
    return t == patch_fighter ? "fighter" : t == patch_corvette ? "corvette" : t == patch_capital ? "capital" : "?";
}
// How the tier was decided: the TShips class id (col 52), a dock or factory, or the radius bands (any other class, an
// unknown id or an unreadable record).
enum TierSource : std::uint8_t { tier_by_radius = 0, tier_by_ship_class = 1, tier_by_station = 2 };
inline const char* tier_source_name(unsigned t) noexcept {
    return t == tier_by_ship_class ? "ship_class" : t == tier_by_station ? "station" : "radius";
}
// colour words: r | g << 8 | b << 16, bit 24 = present.
constexpr std::uint32_t rgb_present = 0x1000000u;
constexpr std::uint32_t pack_rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept {
    return (r > 255 ? 255 : r) | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b) << 16 | rgb_present;
}
// One event, 64 bytes, written by the hook on the game thread and read by the stage (scene end) and Present on the same
// thread. pos is the engine's own integer result in the owner's frame (engine units, 500 per metre); the stage reads the
// owner's rows at scene end. The note's serial (object_lifetime) and the projectile axis are not carried in step 1: the
// handler calls nothing, and the shell needs only the hit direction (owner validation is obj+0x70 == node,
// node+0x28 == handle and the dying bit obj+0x40 & 0x8000000 clear).
struct Event {
    std::uint8_t role, style, tier, flags;   // Role, shape | style_burst, Tier, EventFlag
    std::uint8_t table, tint_source;         // TableId, TintSource
    std::uint16_t row;                       // TBullets / TMissiles index (the object's subtype)
    std::uint32_t t0_game_ms;                // cfg+0x718 at the hook
    std::uint16_t lifetime_ms;               // game ms
    std::uint8_t detail, calibre_q;          // EventDetail bits of an impact (a muzzle record of a later step carries its
                                             // gun slot here); calibre override x 255 (flag_calibre_override)
    std::uint32_t tint_rgb;                  // the classifier's colour (rgb_present | b << 16 | g << 8 | r)
    // The other tint candidate (light <-> texture) narrowed to RGB 3:3:2 (0: none; the weapon_tint / tint_mix override
    // only), and the projectile's travel in the hit object's frame at the hit: its root node's local z (node+0x40 row 2)
    // rotated into the owner's rows (as pos) x 127, rounded; 0, 0, 0 when its node is not live or the row is zero (the
    // stage then takes the radial region, Run235 Flight B fix). Owner-local, so a turning owner keeps its regions.
    std::uint8_t alt_rgb332;
    std::int8_t dir[3];
    std::int32_t damage;                     // the damage routine's running total at the call ([esp+0x1c])
    std::uint32_t owner, node, handle;       // the hit object, its root node obj+0x70 and node+0x28
    // The note's `shooter` is not carried ([[source+0x50]+4] is not dereferenced by the engine on this path, review F9);
    // its four bytes hold the hit object's shield state and patch tier (5.1.1).
    std::uint16_t shield_q;                  // current / maximum shield points x 65535 (the HUD bar's capacity-weighted
                                             // fraction, spawn note 9), post-hit; valid only with flag_shield_read
    std::uint8_t patch_tier, tier_source;    // PatchTier, TierSource of the hit object
    std::int32_t pos[3];                     // the contact point in the owner's frame (engine units)
    std::int32_t radius;                     // obj+0xa4 of the owner (engine units)
    std::uint32_t sequence;                  // monotonic, 0 = free slot
};
static_assert(sizeof(Event) == 64, "the event record is 64 bytes (64 KB ring at 1024 records)");
inline unsigned hull_class_of(const Event& e) noexcept {
    const unsigned c = unsigned(e.detail) >> detail_class_shift;
    return c < hull_class_count ? c : unsigned(class_energy);
}

// ---------------------------------------------------------------------------------------------- engine layout
// Object: class word +0x48, subtype +0x4a, flags +0x40, extension +0x50, root node +0x70, radius +0xa4.
// Node: handle +0x28, position +0x30 (3 x int32), rotation +0x40 (3 rows of 4 dwords, 16.16; row k = local axis k in
// world), +0x180 (3 x int32, the callee's contact offset, unknown 1), +0x1c non-zero on a live node (the callee's test).
// Bullet extension: owner +4 (not read by the handler). Type tables: class c at *(0x00606fb8 + 4c) (count
// *(0x00607038 + 4c)), stride 0xdb8: TBullets c = 0, TShips 7, TShields 9, TMissiles 10. TBullets: shield damage +0x44,
// flags +0x58, light colour +0x5c/+0x60/+0x64, hull damage +0x80. TShips: the OBJ_SHIP_* class id +0xc8 (col 52, spawn
// note 8.1). TShields: capacity +0x48 (col 8, hull points). Engine config *0x00606f34: the game clock +0x718 (game ms,
// SETA-scaled).
// Shields (spawn note 9): an object has them when its class is 7, 1 or 0x12 or obj+0x40 & 0x100000; they are the
// class-9 objects of the list [obj+0x50]+0x6c chained through node+0 (the node whose +0 is 0 is the end sentinel, not a
// shield), with the TShields index node+0x4a (int16) and the energy [node+0x50]+4 (16.16 thousandths of the capacity:
// full = 0x3e80000).
namespace layout {
constexpr unsigned obj_flags = 0x40, obj_class = 0x48, obj_subtype = 0x4a, obj_ext = 0x50, obj_node = 0x70,
                   obj_radius = 0xa4;
constexpr unsigned node_live = 0x1c, node_handle = 0x28, node_pos = 0x30, node_rot = 0x40, node_contact = 0x180;
constexpr unsigned ext_owner = 4, ext_shield_list = 0x6c;
constexpr unsigned shield_next = 0, shield_index = 0x4a, shield_ext = 0x50, shield_energy = 4, shield_capacity = 0x48;
constexpr std::int32_t shield_full = 0x3e80000;
constexpr unsigned shield_list_max = 64;
constexpr unsigned type_stride = 0xdb8;
constexpr unsigned bullet_shield_damage = 0x44, bullet_flags = 0x58, bullet_light = 0x5c, bullet_hull_damage = 0x80;
constexpr unsigned ship_class_id = 0xc8;
constexpr unsigned cfg_game_ms = 0x718;
// The ship's hull walk (render-node-bounds.md 6.1; ship_avoidance_walk_core.h hull_children): node links, placement,
// scales, flags and body id; the body cache's hash, its model's LOD records and their extents.
constexpr std::uintptr_t body_cache_root = 0x00608518;
constexpr unsigned body_cache_hash = 0x14, node_children = 0x0c, node_sentinel = 0x10, node_offset = 0x30, node_basis = 0x40,
                   node_scale = 0x70, node_axis_scale = 0x80, node_flags = 0x12c, node_body = 0x140, node_walk_bytes = 0x144;
constexpr std::uint32_t node_scaled = 0x40, node_unscaled_offset = 0x10000, node_no_collision = 0x8000000;
constexpr unsigned model_lods = 0x0c, lod_extent = 0x20; // the LOD count, an int16, follows the array pointer
constexpr std::uint32_t obj_dying = 0x8000000u;    // obj+0x40 bit set once by the kill routine 0x0044abd0
constexpr std::uint32_t obj_shielded = 0x100000u;  // obj+0x40 bit 20: docks, factories, ships at creation
constexpr std::uint16_t class_bullet = 0, class_shielded_a = 1, class_dock = 5, class_factory = 6, class_ship = 7,
                        class_missile = 10, class_asteroid = 17, class_shielded_b = 0x12;
}
struct Globals {                     // the engine's slots (X3AP.exe VAs in production; the fixture maps the same pages)
    std::uintptr_t cfg_slot = 0x00606f34;
    std::uintptr_t bullet_table_slot = 0x00606fb8, bullet_count_slot = 0x00607038;
    std::uintptr_t missile_table_slot = 0x00606fe0, missile_count_slot = 0x00607060;
    std::uintptr_t ship_table_slot = 0x00606fd4, ship_count_slot = 0x00607054;
    std::uintptr_t shield_table_slot = 0x00606fdc, shield_count_slot = 0x0060705c;
};

// ---------------------------------------------------------------------------------------------- the classifier (4)
// TBullets flag bits (col 12, +0x58).
constexpr std::uint32_t bullet_beam_bits = 0x6, bullet_wave_bit = 0x8, bullet_hull_through_shields = 0x20,
                        bullet_burst_bits = 0x1100;
// The hull-hit classifier's cues (hull-impact-classes.md section 2): ammunition 0x40 (mass drivers), repair 0x80, the
// on-hit bits 0x200 (slow) and 0x400 (energy drain: the ion cues).
constexpr std::uint32_t bullet_ammunition_bit = 0x40, bullet_repair_bit = 0x80, bullet_on_hit_bits = 0x600;
// 4.1 shape thresholds on the vanilla body's z_over_xy (the table generator and the DLL's table loader apply them).
constexpr float tracer_min_elongation = 4.0f, slug_min_elongation = 1.5f;
// 4.2 tint: the light colour when max >= tint_light_min and (max - min) / max >= tint_saturation_min.
constexpr std::uint32_t tint_light_min = 64;
constexpr float tint_saturation_min = 0.25f;
constexpr std::uint32_t tint_saturation_quarters = 1; // the same rule in integers: 4 (max - min) >= 1 x max
// 4.2 calibre: bands on d = max(shield, hull) per shot (impacts: the running total), k = clamp(log10(d / ref) / 2, 0, 1).
constexpr std::int32_t calibre_medium_min = 3000, calibre_heavy_min = 20000;
constexpr float calibre_reference = 300.0f, calibre_decades = 2.0f;
// missile_hit: the fixed look of a missile impact (medium calibre, warm tint).
constexpr std::uint32_t missile_tint_rgb = 0x1000000u | 255u | 170u << 8 | 90u << 16;

inline std::uint8_t shape_from_elongation(float e, bool present) noexcept {
    if (!present || !(e == e)) return shape_slug; // no table entry: the middle shape
    return e >= tracer_min_elongation ? shape_tracer : e >= slug_min_elongation ? shape_slug : shape_orb;
}
// The style from the live flag word and the table's elongation shape (rule order of 4.1).
inline std::uint8_t style_from(std::uint32_t flags, std::uint8_t table_shape) noexcept {
    const std::uint8_t shape = (flags & bullet_beam_bits) ? std::uint8_t(shape_beam)
                               : (flags & bullet_wave_bit) ? std::uint8_t(shape_wave) : table_shape;
    return std::uint8_t(shape | ((flags & bullet_burst_bits) ? style_burst : 0));
}
inline std::uint8_t tier_from(std::int32_t d) noexcept {
    return d >= calibre_heavy_min ? tier_heavy : d >= calibre_medium_min ? tier_medium : tier_light;
}
// The light colour (cols 13-15) chosen as tint when saturated and bright enough (integers).
inline bool light_saturated(std::int32_t r, std::int32_t g, std::int32_t b) noexcept {
    std::int32_t hi = r > g ? r : g, lo = r < g ? r : g;
    hi = hi > b ? hi : b;
    lo = lo < b ? lo : b;
    if (lo < 0) lo = 0;
    return hi >= std::int32_t(tint_light_min) && 4 * (hi - lo) >= std::int32_t(tint_saturation_quarters) * hi;
}
inline std::uint32_t clamp_channel(std::int32_t v) noexcept {
    return v < 0 ? 0u : v > 255 ? 255u : std::uint32_t(v);
}
// The hull-hit class of a live record (hull-impact-classes.md section 2, first match wins; integers only, the handler's
// values): beam flags with no per-shot damage: the tug; repair 0x80: the repair laser (*variant, the class beam for a
// beam row); beam flags: beam; the on-hit bits: ion; ammunition: ballistic, heavy in the heavy band; an orb or wave body:
// plasma; the heavy band: heavy; else energy. *variant gets detail_repair / detail_tug or 0. d is the bullet record's
// own per-shot damage, max(shield +0x44, hull +0x80) (the census script's), never the applied total: a shields-down hit's
// total is its hull damage alone (review 2026-11-07: stock PPC read energy, GAUSS ballistic), and a target that takes
// no damage must not turn a damaging beam into the tug.
static inline __attribute__((always_inline)) std::uint8_t hull_class_from(std::uint32_t flags, std::int32_t d,
                                                                       std::uint8_t style, std::uint8_t* variant) noexcept {
    *variant = 0;
    const bool beam = (flags & bullet_beam_bits) != 0, heavy = d >= calibre_heavy_min;
    if (beam && d <= 0) {
        *variant = detail_tug;
        return class_beam;
    }
    if (flags & bullet_repair_bit) {
        *variant = detail_repair;
        return beam ? std::uint8_t(class_beam) : std::uint8_t(class_energy);
    }
    if (beam) return class_beam;
    if (flags & bullet_on_hit_bits) return class_ion;
    if (flags & bullet_ammunition_bit) return heavy ? std::uint8_t(class_heavy) : std::uint8_t(class_ballistic);
    const std::uint8_t shape = std::uint8_t(style & style_shape_mask);
    if (shape == shape_orb || shape == shape_wave) return class_plasma;
    return heavy ? std::uint8_t(class_heavy) : std::uint8_t(class_energy);
}
// The shield rule (spawn note section 3): a shield-hit look is absorbed == 1 && !(class 0 source && flag 0x20).
inline std::uint8_t role_from(std::uint32_t absorbed, std::uint16_t projectile_class, std::uint32_t bullet_flags) noexcept {
    if (absorbed != 1) return role_hull_hit;
    if (projectile_class == layout::class_bullet && (bullet_flags & bullet_hull_through_shields)) return role_hull_hit;
    return role_shield_hit;
}

// ---------------------------------------------------------------------------------------------- the generated table
// <game>\x3m\weapon_effects.json (tools/effects/weapon_effects_table.py), reduced at load to the integers the handler
// indexes. rows = the TBullets row count the file was generated from: a live count *0x00607038 that differs disables
// the lookups (stale), never the hooks.
constexpr unsigned table_capacity = 512;
enum RowBit : std::uint8_t { row_present = 1, row_style_override = 2, row_tint_override = 4, row_calibre_override = 8,
                             row_texture = 16, row_hull_repair = 32, row_hull_tug = 64, row_hull_class = 128 };
struct Table {
    std::uint32_t rows = 0;           // TBullets rows of the generating game tree, 0 = no table
    std::uint32_t missile_rows = 0;
    std::uint8_t bits[table_capacity]{};
    std::uint8_t shape[table_capacity]{};     // from the elongation (or the override's style)
    std::uint8_t calibre_q[table_capacity]{}; // override x 255
    std::uint32_t texture_rgb[table_capacity]{};
    std::uint32_t override_rgb[table_capacity]{};
    std::uint8_t hull_class[table_capacity]{}; // the override's class pin (HullClass, with row_hull_class)
};

// ---------------------------------------------------------------------------------------------- 16.16 arithmetic
// The engine's product: (int64(a) * b + 0x8000) >> 16, low 32 bits (0x004f0da0 / 0x004f0c00: imul, add 0x8000, adc,
// shrd 16). Integer only.
inline std::int32_t fx(std::int32_t a, std::int32_t b) noexcept {
    const std::int64_t p = std::int64_t(a) * std::int64_t(b) + 0x8000;
    return std::int32_t(std::uint32_t(std::uint64_t(p) >> 16));
}
inline std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    return std::int32_t(std::uint32_t(a) + std::uint32_t(b));
}
inline std::int32_t wrap_sub(std::int32_t a, std::int32_t b) noexcept {
    return std::int32_t(std::uint32_t(a) - std::uint32_t(b));
}
// 0x004f0da0: out_i = sum_j fx(M[j][i], v_j) (rows at a 16-byte stride: the local -> world rotation).
inline void rotate_out(const std::int32_t* m, const std::int32_t* v, std::int32_t* out) noexcept {
    for (unsigned i = 0; i < 3; ++i)
        out[i] = wrap_add(wrap_add(fx(m[i], v[0]), fx(m[4 + i], v[1])), fx(m[8 + i], v[2]));
}
// 0x004f0c00: out_i = sum_j fx(M[i][j], v_j) (world -> local).
inline void rotate_in(const std::int32_t* m, const std::int32_t* v, std::int32_t* out) noexcept {
    for (unsigned i = 0; i < 3; ++i)
        out[i] = wrap_add(wrap_add(fx(m[4 * i], v[0]), fx(m[4 * i + 1], v[1])), fx(m[4 * i + 2], v[2]));
}

// ---------------------------------------------------------------------------------------------- the ring (3.1)
// 1024 records (64 KB; 256 until 2026-11-07): a 60-80 ship battle's live hits. The free list's u16 indices hold it.
constexpr unsigned ring_capacity = 1024;
static_assert(ring_capacity <= 0x10000u, "the free list holds u16 slot indices");
struct Ring {
    Event slots[ring_capacity];
    std::uint16_t free_list[ring_capacity];
    // Per slot, the stage's verdict on the record's owner in its last frame (ring_stamp_hidden): hidden_undrawn (not
    // drawn: past its owner cap, off screen, beyond the far plane; the overflow order, ring_victim) and hidden_off_screen
    // (its sphere outside the view volume, whether kept or not; the stages' visible-first owner order). 0 for a record
    // acquired since that frame (not judged yet): a fresh hit keeps its afterglow, a new owner counts as visible.
    std::uint8_t hidden[ring_capacity];
    unsigned free_count = 0;
    std::uint32_t next_sequence = 1;
    // overflow: records evicted; evicted_visible: of them records not stamped hidden_undrawn (drawn, or not judged yet)
    std::uint32_t overflow = 0, retired = 0, evicted_visible = 0;
};
constexpr std::uint8_t hidden_undrawn = 1, hidden_off_screen = 2;
inline void ring_clear(Ring& r) noexcept {
    for (unsigned i = 0; i < ring_capacity; ++i) {
        r.slots[i].sequence = 0;
        r.free_list[i] = std::uint16_t(ring_capacity - 1 - i);
        r.hidden[i] = 0;
    }
    r.free_count = ring_capacity;
}
inline void ring_release(Ring& r, unsigned i) noexcept {
    if (i >= ring_capacity || !r.slots[i].sequence) return;
    r.slots[i].sequence = 0;
    r.free_list[r.free_count++] = std::uint16_t(i);
}
// Overflow order (only when full), four classes, the oldest (lowest sequence) first within each: census-only records
// (the stage does not draw them: their native spawn ran) whose owner the stage did not draw in its last frame, the
// other census-only records, stage-owned records of any role (their native spawn was skipped, so evicting one loses
// the effect: a staged hull hit is no cheaper than a staged shield hit) whose owner was not drawn, the other
// stage-owned records. "Not drawn" is the stage's per-slot stamp (Ring::hidden & hidden_undrawn); the handler reads it
// with integer compares in the one scan it already made, and calls nothing.
inline unsigned ring_victim(const Ring& r) noexcept {
    unsigned best = ring_capacity, best_rank = 4;
    std::uint32_t best_seq = 0xffffffffu;
    for (unsigned i = 0; i < ring_capacity; ++i) {
        const Event& e = r.slots[i];
        if (!e.sequence) continue;
        const unsigned rank = ((e.flags & flag_stage) ? 2u : 0u) | ((r.hidden[i] & hidden_undrawn) ? 0u : 1u);
        if (rank < best_rank || (rank == best_rank && e.sequence < best_seq)) {
            best_rank = rank;
            best_seq = e.sequence;
            best = i;
        }
    }
    return best;
}
// A slot for a new event (sequence assigned, not hidden: not judged yet); never fails on a cleared ring.
inline Event* ring_acquire(Ring& r) noexcept {
    unsigned i;
    if (r.free_count)
        i = r.free_list[--r.free_count];
    else {
        i = ring_victim(r);
        if (i >= ring_capacity) return nullptr;
        ++r.overflow;
        if (!(r.hidden[i] & hidden_undrawn)) ++r.evicted_visible; // the flight's row: evictions that cost a drawn effect
    }
    Event* e = &r.slots[i];
    e->sequence = r.next_sequence++;
    if (!r.next_sequence) r.next_sequence = 1;
    r.hidden[i] = 0;
    return e;
}
// Whether a stage-owned (flag_stage) record is live: the stage's callback is set only then.
inline bool ring_owned_live(const Ring& r) noexcept {
    if (r.free_count >= ring_capacity) return false;
    for (unsigned i = 0; i < ring_capacity; ++i)
        if (r.slots[i].sequence && (r.slots[i].flags & flag_stage)) return true;
    return false;
}
// Every live record of an owner (a death record, a failed validation) leaves the ring; returns how many.
// keep_staged_hull (the shield stage's retirement): a stage-owned hull hit stays, the hull stage decides its own (its
// sparks outlive the owner once they left it).
inline unsigned ring_retire_owner(Ring& r, std::uint32_t owner, bool keep_staged_hull = false) noexcept {
    unsigned n = 0;
    for (unsigned i = 0; i < ring_capacity; ++i)
        if (r.slots[i].sequence && r.slots[i].owner == owner &&
            !(keep_staged_hull && r.slots[i].role == role_hull_hit && (r.slots[i].flags & flag_stage))) {
            ring_release(r, i);
            ++n;
        }
    r.retired += n;
    return n;
}
// The stages' verdicts on the owners of one frame (stage::build and stage::build_hull add theirs): drawn (read, kept
// within the owner cap and in reach: the shield stage's owner_in_view sphere, the hull stage's reach sphere) and in view
// (every owner of a candidate record whose sphere reaches the view volume, kept or not; an owner past a cap is located
// through stage::locate_owner). far_z is the caller's input (the projection's far plane, view units; 0: none), kept
// far_margin beyond it (the plane recovered from a float projection is about 2 % off). ring_stamp_hidden turns the
// verdicts into Ring::hidden. The located table: the owners read this frame (key owner, node, handle; position, radius
// and the read's status), so both stages read an owner at most once per frame however many of its records they visit;
// its entries carry the frame's generation, so reset clears nothing.
struct VisibleOwners {
    static constexpr unsigned capacity = 64;                     // drawn: >= max_owners + hull::max_owners (asserted)
    static constexpr unsigned view_capacity = 2 * ring_capacity; // in view: every candidate of both stages
    static constexpr float far_margin = 1.05f;
    static constexpr unsigned located_bits = 11, located_size = 1u << located_bits; // at most ring_capacity keys
    struct Located {
        std::uint32_t owner = 0, node = 0, handle = 0, generation = 0; // generation 0: never current
        std::int32_t pos[3]{}, radius = 0;
        std::uint8_t status = 0; // stage::OwnerStatus
    };
    float far_z = 0.f;
    unsigned count = 0, in_view_count = 0;
    unsigned located_count = 0, located_reads = 0; // this frame: owners in the table, those read from memory
    std::uint32_t generation = 1;
    std::uint32_t owner[capacity];
    std::uint32_t in_view[view_capacity];
    Located located[located_size];
    void add(std::uint32_t o) noexcept { // drawn (once)
        for (unsigned k = 0; k < count; ++k)
            if (owner[k] == o) return;
        if (count < capacity) owner[count++] = o;
    }
    void add_in_view(std::uint32_t o) noexcept { // repeats allowed (both stages may add an owner)
        if (in_view_count < view_capacity) in_view[in_view_count++] = o;
    }
    void reset(float far_plane) noexcept {
        far_z = far_plane > 0.f ? far_plane * far_margin : 0.f;
        count = in_view_count = located_count = located_reads = 0;
        if (!++generation) { // the wrap (once in 2^32 frames): no entry may carry the new generation
            for (auto& l : located) l.generation = 0;
            generation = 1;
        }
    }
    // The entry of (o, node, handle): its own this frame, or the free one it would take; nullptr when the table is full.
    Located* find(std::uint32_t o, std::uint32_t node, std::uint32_t handle) noexcept {
        unsigned h = unsigned(((o ^ (node * 0x85ebca6bu)) * 0x9e3779b1u) >> (32u - located_bits));
        for (unsigned probe = 0; probe < located_size; ++probe, h = (h + 1u) & (located_size - 1u)) {
            Located& l = located[h];
            if (l.generation != generation) return located_count < located_size / 2 ? &l : nullptr;
            if (l.owner == o && l.node == node && l.handle == handle) return &l;
        }
        return nullptr;
    }
};
static_assert(2 * ring_capacity <= VisibleOwners::located_size, "the located table stays at most half full");
// Once per stage frame, after the builds: Ring::hidden for every live record from its owner's verdicts (hidden_undrawn
// unless drawn, hidden_off_screen unless in view). The owners in a 2048-entry open-addressed table (at most 1024
// distinct, half full), one probe or two per record, O(ring_capacity).
inline void ring_stamp_hidden(Ring& r, const VisibleOwners& v) noexcept {
    constexpr unsigned bits = 11, size = 1u << bits;
    static_assert(2 * ring_capacity <= size, "the table stays at most half full");
    std::uint32_t key[size];
    std::uint8_t flag[size]; // 0 empty; 1 drawn, 2 in view
    for (unsigned k = 0; k < size; ++k) flag[k] = 0;
    const auto slot = [&](std::uint32_t o) {
        unsigned h = unsigned((o * 0x9e3779b1u) >> (32u - bits));
        while (flag[h] && key[h] != o) h = (h + 1u) & (size - 1u);
        return h;
    };
    const auto put = [&](std::uint32_t o, std::uint8_t f) {
        const unsigned h = slot(o);
        key[h] = o;
        flag[h] = std::uint8_t(flag[h] | f);
    };
    for (unsigned k = 0; k < v.count; ++k) put(v.owner[k], 1);
    for (unsigned k = 0; k < v.in_view_count; ++k) put(v.in_view[k], 2);
    for (unsigned i = 0; i < ring_capacity; ++i) {
        if (!r.slots[i].sequence) continue;
        const unsigned h = slot(r.slots[i].owner);
        const std::uint8_t f = flag[h];
        r.hidden[i] = std::uint8_t(((f & 1) ? 0 : hidden_undrawn) | ((f & 2) ? 0 : hidden_off_screen));
    }
}

// ---------------------------------------------------------------------------------------------- the handler (2.2)
// mode_shields_hull (step 2, docs/architecture/weapon-look.md 3.1 and 2.12): the shield hit as in shields mode plus the
// hull hit (family_hull). Both are the stage's modes (stage_mode): the ownership latch, the ring's scope and the stage.
enum Mode : std::uint8_t { mode_native = 0, mode_observe = 1, mode_shields = 2, mode_shields_hull = 3 };
inline const char* mode_name(unsigned m) noexcept {
    return m == mode_observe ? "observe" : m == mode_shields ? "shields" : m == mode_shields_hull ? "shields,hull" : "native";
}
inline bool stage_mode(unsigned m) noexcept {
    return m == mode_shields || m == mode_shields_hull;
}
// X3M_WEAPON_EFFECTS (lower case): one word, "native", "observe" or "shields", or the family list "shields,hull" in either
// order ("hull,shields"); spaces around each token are ignored. Unset or empty = native (note 2.6). Any other token
// ("explosions", "bolts", "beams": later steps), "hull" alone, an empty or repeated token is refused.
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, Mode* out) noexcept {
    static const char* const words[4] = {"native", "observe", "shields", "hull"};
    if (!text || !n) return false;
    unsigned seen = 0, tokens = 0, single = 4;
    std::size_t at = 0;
    for (;;) {
        std::size_t end = at;
        while (end < n && text[end] != Char(',')) ++end;
        std::size_t a = at, b = end;
        while (a < b && text[a] == Char(' ')) ++a;
        while (b > a && text[b - 1] == Char(' ')) --b;
        unsigned w = 0;
        for (; w < 4; ++w) {
            std::size_t len = 0;
            while (words[w][len]) ++len;
            if (b - a != len) continue;
            bool same = true;
            for (std::size_t i = 0; i < len && same; ++i) same = text[a + i] == Char(words[w][i]);
            if (same) break;
        }
        if (w >= 4 || (seen & (1u << w))) return false; // an unknown, empty or repeated token
        seen |= 1u << w;
        single = w;
        ++tokens;
        if (end >= n) break;
        at = end + 1;
    }
    if (tokens == 1 && single < 3) {
        *out = Mode(single);
        return true;
    }
    if (tokens == 2 && seen == ((1u << 2) | (1u << 3))) {
        *out = mode_shields_hull;
        return true;
    }
    return false;
}
constexpr Mode default_mode = mode_native;
constexpr unsigned setting_capacity = 16;

struct PosCheck {           // observe mode: the handler's pos against the inner tap's (weapon_pos_check)
    std::uint32_t owner = 0, sequence = 0;
    std::int32_t pos[3]{};
    bool pending = false;
    std::uint32_t compared = 0, unmatched = 0;
    std::uint32_t max_error = 0; // max |component| difference (engine units)
};
struct Counters {
    std::uint32_t calls = 0, recorded = 0, skipped = 0, forwarded = 0, no_projectile = 0, no_node = 0, stale_table = 0,
                  taps = 0, shield_hits = 0, hull_hits = 0, nested = 0;
    std::uint32_t dir_fallback = 0; // recorded without a travel direction (projectile node not live, or a zero row)
    std::uint32_t shield_read = 0, shield_unread = 0; // the hit object's shield list walked to its sentinel, or refused
    std::uint32_t drained = 0; // absorbed hits on a drained shield recorded as hull hits (drained_shield_q)
};
struct HookState {
    std::uint8_t mode = mode_native;   // what initialize() installed (native: the stubs are not live)
    std::uint8_t armed = 0;            // families whose records the stage draws (bit 0: shields)
    volatile std::uint8_t owned_next = 0; // Present's latch: the stage owned the last frame
    std::uint8_t suppress_native = suppress_native_shield_hit; // a stage-owned shield hit also skips the native spawn
    std::uint8_t suppress_native_hull = suppress_native_hull_hit; // a stage-owned hull hit also skips it (step 2)
    Globals globals{};
    const Table* table = nullptr;      // the loaded table (null: classifier without table lookups)
    std::uint16_t lifetime_ms = 0;     // the records' game-time life (install: patch_lifetime_ms)
    std::uint16_t hull_lifetime_ms = 0; // a hull hit's game-time life (install: hull_lifetime_ms)
    // The stage's three-frame floor in game ms (written by the stage each call; Present only reads it): Present keeps a
    // stage-owned hull hit that much past its lifetime, so a floored element still draws its frames.
    std::uint32_t life_floor_ms = 0;
    // Present's latch beside owned_next (shields,hull): the pass holds the hull programs (a device that refused them
    // keeps the shield effect, and its hull hits forward).
    volatile std::uint8_t hull_ready = 0;
    Ring ring{};
    PosCheck check{};
    Counters counters{};
};
constexpr std::uint8_t family_shields = 1, family_hull = 2;

inline std::uint32_t rd32(std::uintptr_t a) noexcept {
    return *reinterpret_cast<const volatile std::uint32_t*>(a);
}
inline std::uint16_t rd16(std::uintptr_t a) noexcept {
    return *reinterpret_cast<const volatile std::uint16_t*>(a);
}
// A live node of an object, mirroring the callee's tests (obj+0x70 non-null, node+0x1c non-zero); 0 otherwise.
inline std::uintptr_t live_node(std::uintptr_t obj) noexcept {
    if (!obj) return 0;
    const std::uintptr_t node = rd32(obj + layout::obj_node);
    return node && rd32(node + layout::node_live) ? node : 0;
}
inline void read3(std::uintptr_t a, std::int32_t* out) noexcept {
    for (unsigned i = 0; i < 3; ++i) out[i] = std::int32_t(rd32(a + 4 * i));
}
inline void read_rows(std::uintptr_t a, std::int32_t* out) noexcept {
    for (unsigned i = 0; i < 12; ++i) out[i] = std::int32_t(rd32(a + 4 * i));
}

// The hit object's shields at the stub (spawn note 9's recipe; the bounds are the stub's own): current / maximum shield
// points as a fraction x 65535 in *q, capacity-weighted like the HUD bar (0x0044ecd0 / 0x0044ee10), post-hit. Integer
// only, plain loads, no call and no 64-bit division (the i686 build would call __udivdi3): each shield's energy (16.16
// thousandths, clamped to [0, full]) is reduced to 1/65536 steps of its capacity (energy / 1000, a 32-bit division by a
// constant), the two sums are 64-bit, and the ratio is one 32-bit division after both are shifted below 2^16. The
// per-shield truncation of 0x0044ecd0 is not replicated: the fraction is within 2^-15 of the HUD's ratio. Returns false
// (no fraction: the colour falls back to full shields) for an unshielded class, a null extension or list head, a
// TShields index outside the count, a null shield extension, a 65th node that is not the sentinel, or no capacity at
// all; one bad node refuses the whole read rather than reporting a partial sum.
static inline __attribute__((always_inline)) bool read_shields(std::uintptr_t obj, std::uint16_t cls, const Globals& g,
                                                                std::uint16_t* q) noexcept {
    const std::uint32_t flags = rd32(obj + layout::obj_flags);
    if (!(cls == layout::class_ship || cls == layout::class_shielded_a || cls == layout::class_shielded_b ||
          (flags & layout::obj_shielded)))
        return false;
    const std::uintptr_t ext = rd32(obj + layout::obj_ext);
    if (!ext) return false;
    const std::uintptr_t table = rd32(g.shield_table_slot);
    const std::int32_t count = std::int32_t(rd32(g.shield_count_slot));
    if (!table || count <= 0) return false;
    std::uintptr_t node = rd32(ext + layout::ext_shield_list);
    if (!node) return false;
    std::uint64_t points = 0, capacity = 0; // sum cap_i x (energy_i / 1000) and sum cap_i
    for (unsigned k = 0;; ++k) {
        const std::uintptr_t next = rd32(node + layout::shield_next);
        if (!next) break; // the sentinel: every shield before it is counted
        if (k >= layout::shield_list_max) return false;
        const std::int32_t index = std::int16_t(rd16(node + layout::shield_index));
        if (index < 0 || index >= count) return false;
        const std::uintptr_t shield = rd32(node + layout::shield_ext);
        if (!shield) return false;
        const std::int32_t cap =
            std::int32_t(rd32(table + std::uintptr_t(index) * layout::type_stride + layout::shield_capacity));
        std::int32_t energy = std::int32_t(rd32(shield + layout::shield_energy));
        energy = energy < 0 ? 0 : energy > layout::shield_full ? layout::shield_full : energy;
        if (cap > 0) {
            points += std::uint64_t(std::uint32_t(cap)) * (std::uint32_t(energy) / 1000u);
            capacity += std::uint32_t(cap);
        }
        node = next;
    }
    if (!capacity) return false;
    const std::uint64_t whole = capacity << 16; // points <= whole (energy / 1000 <= 65536)
    unsigned shift = 0;
    while ((whole >> shift) > 0xffffu) ++shift;
    const std::uint32_t d = std::uint32_t(whole >> shift), n = std::uint32_t(points >> shift);
    *q = std::uint16_t(n >= d ? 0xffffu : n * 0xffffu / d);
    return true;
}
// The patch tier from the TShips class id (col 52, spawn note 8.1); 0xff for an id outside the AP / FL tables. Fighters:
// M3/M4/M5, drones, satellites, beacons, mines, the astronaut, UFO, spacefly, the Khaak cluster; corvettes: M6, M8,
// TS, TP, TM, the pirate TS, the Goner ship; capitals: M0, M1, M2, M7, TL, the pirate TL, the orbital lasers and FL's
// drone carrier and missile frigate.
inline std::uint8_t patch_tier_of_ship(std::int32_t id) noexcept {
    switch (id) {
    case 2023: case 2024: case 2025: case 2060: case 2062: case 2064: case 2065: case 2066: case 2067: case 2068:
    case 2070: case 2080: case 2092: case 2131: case 2143: case 2144: case 2145: case 2146: case 2147: case 2150:
    case 2151:
        return patch_fighter;
    case 2026: case 2030: case 2031: case 2035: case 2039: case 2141: case 2142:
        return patch_corvette;
    case 2011: case 2020: case 2021: case 2022: case 2027: case 2032: case 2036: case 2149: case 2152: case 2153:
        return patch_capital;
    default:
        return 0xff;
    }
}
// The radius bands for any other object (obj+0xa4, engine units; [I] until the observe flight logs radius with the
// class): below 30 m a fighter, below 150 m a corvette, else a capital.
constexpr std::int32_t tier_corvette_min_u = 15000, tier_capital_min_u = 75000;
inline std::uint8_t patch_tier_of_radius(std::int32_t r) noexcept {
    return r >= tier_capital_min_u ? patch_capital : r >= tier_corvette_min_u ? patch_corvette : patch_fighter;
}
// The hit object's patch tier: a dock or factory is a capital; a ship takes its TShips class id (subtype bounded by
// the table's count, a null table refused); anything else, or an unknown id, the radius bands.
static inline __attribute__((always_inline)) void read_tier(std::uintptr_t obj, std::uint16_t cls, std::int32_t radius,
                                                             const Globals& g, std::uint8_t* tier,
                                                             std::uint8_t* source) noexcept {
    if (cls == layout::class_dock || cls == layout::class_factory) {
        *tier = patch_capital;
        *source = tier_by_station;
        return;
    }
    if (cls == layout::class_ship) {
        const std::uintptr_t table = rd32(g.ship_table_slot);
        const std::int32_t count = std::int32_t(rd32(g.ship_count_slot));
        const std::int32_t sub = std::int16_t(rd16(obj + layout::obj_subtype));
        if (table && sub >= 0 && sub < count) {
            const std::uint8_t t = patch_tier_of_ship(
                std::int32_t(rd32(table + std::uintptr_t(sub) * layout::type_stride + layout::ship_class_id)));
            if (t != 0xff) {
                *tier = t;
                *source = tier_by_ship_class;
                return;
            }
        }
    }
    *tier = patch_tier_of_radius(radius);
    *source = tier_by_radius;
}

// A unit 16.16 row (a node rotation row) as three int8 x 127, rounded half up (each component clamped to +-1 first).
static inline __attribute__((always_inline)) void quantize_dir(const std::int32_t row[3], std::int8_t out[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        const std::int32_t r = row[i] > 0x10000 ? 0x10000 : row[i] < -0x10000 ? -0x10000 : row[i];
        out[i] = std::int8_t((r * 127 + 0x8000) >> 16); // arithmetic shift: floor((r x 127) / 65536 + 1/2)
    }
}
// An rgb_present colour word as RGB 3:3:2 (the top bits of each channel); 0 for none.
static inline __attribute__((always_inline)) std::uint8_t rgb332(std::uint32_t word) noexcept {
    if (!(word & rgb_present)) return 0;
    return std::uint8_t(((word & 0xe0u)) | ((word >> 11) & 0x1cu) | ((word >> 22) & 0x3u));
}
// The impact site 0x00449d85 -> 0x00413c40(victim, source). `esp` is the stack at stub entry: [1] victim, [2] source,
// [7] the running damage total, [9] the absorbed flag. Integer only, plain loads, no call. Returns 1 to skip the
// engine's spawn (stage-owned shield hit), 0 to forward. now_game is read here (cfg+0x718).
// Internal linkage and always_inline (review F10): every translation unit that includes this header gets its own copy
// compiled with its own flags, so the no-SSE handler TU (weapon_effects_hook.cpp) can never bind a COMDAT instance
// emitted by an SSE2 TU; the hook fixture's audit proves its two entry points contain no call at all.
// The object reads stay inside the routine's own two objects (their class, subtype, flags +0x40, node, radius) and
// nodes, the type table and the engine config: [source+0x50] (the bullet extension) is not followed (review F9).
// flag_nested (note 2.7): the projectile or the hit object already carries the kill routine's dying bit
// (obj+0x40 & 0x8000000, set once by 0x0044abd0 before its area damage 0x0045eeb0 runs the damage routine on the
// blast's objects [i, spawn note sections 4 and 6]): the impact arrives inside a kill. A dying hit object forwards
// (the stage would retire it unseen); a dying projectile (the blast's source) still skips for a live victim.
static inline __attribute__((always_inline)) int impact(const std::uint32_t* esp, HookState& s) noexcept {
    ++s.counters.calls;
    if (s.mode == mode_native) return 0;
    const std::uintptr_t victim = esp[1], source = esp[2];
    const std::uint32_t total = esp[7], absorbed = esp[9];
    if (!victim || !source) {
        ++s.counters.no_projectile;
        return 0;
    }
    const std::uintptr_t victim_node = live_node(victim), source_node = live_node(source);
    if (!victim_node || !source_node) { // the callee returns 0 without an effect
        ++s.counters.no_node;
        return 0;
    }
    // The projectile: the victim when it is class 0 or 10 (tested first), else the source; none: no effect either.
    const std::uint16_t vc = rd16(victim + layout::obj_class), sc = rd16(source + layout::obj_class);
    const bool victim_is_projectile = vc == layout::class_bullet || vc == layout::class_missile;
    const bool source_is_projectile = sc == layout::class_bullet || sc == layout::class_missile;
    if (!victim_is_projectile && !source_is_projectile) {
        ++s.counters.no_projectile;
        return 0;
    }
    const std::uintptr_t projectile = victim_is_projectile ? victim : source;
    const std::uintptr_t hit = victim_is_projectile ? source : victim;
    const std::uintptr_t hit_node = victim_is_projectile ? source_node : victim_node;
    const std::uint16_t pclass = victim_is_projectile ? vc : sc;
    // The contact point, the callee's own arithmetic: the smaller body (victim radius < source radius: the victim's
    // node, else the source's) gives p = node+0x30 + R(node+0x40) node+0x180; pos = R^T(hit) (p - hit node+0x30).
    const std::int32_t vr = std::int32_t(rd32(victim + layout::obj_radius)), sr = std::int32_t(rd32(source + layout::obj_radius));
    const std::uintptr_t small_node = vr >= sr ? source_node : victim_node;
    std::int32_t rows[12], offset[3], p[3], origin[3], d[3], pos[3];
    read_rows(small_node + layout::node_rot, rows);
    read3(small_node + layout::node_contact, offset);
    rotate_out(rows, offset, p);
    read3(small_node + layout::node_pos, origin);
    for (unsigned i = 0; i < 3; ++i) p[i] = wrap_add(p[i], origin[i]);
    read3(hit_node + layout::node_pos, origin);
    for (unsigned i = 0; i < 3; ++i) d[i] = wrap_sub(p[i], origin[i]);
    read_rows(hit_node + layout::node_rot, rows);
    rotate_in(rows, d, pos);
    // The live record and the classifier.
    const std::uint16_t subtype = rd16(projectile + layout::obj_subtype);
    std::uint32_t flags = 0, light = 0;
    std::uint8_t style = shape_slug, table = table_none, tint_source = tint_white, eflags = 0, calibre_q = 0, variant = 0,
                 hull_class = class_energy;
    std::uint32_t tint = 0, alt = 0;
    const bool hit_dying = (rd32(hit + layout::obj_flags) & layout::obj_dying) != 0;
    const bool nested = hit_dying || (rd32(projectile + layout::obj_flags) & layout::obj_dying) != 0;
    if (pclass == layout::class_bullet) {
        table = table_bullets;
        const std::uintptr_t base = rd32(s.globals.bullet_table_slot);
        const std::uint32_t count = rd32(s.globals.bullet_count_slot);
        if (base && subtype < count) {
            const std::uintptr_t rec = base + std::uintptr_t(subtype) * layout::type_stride;
            flags = rd32(rec + layout::bullet_flags);
            const std::int32_t r = std::int32_t(rd32(rec + layout::bullet_light)),
                               g = std::int32_t(rd32(rec + layout::bullet_light + 4)),
                               b = std::int32_t(rd32(rec + layout::bullet_light + 8));
            light = pack_rgb(clamp_channel(r), clamp_channel(g), clamp_channel(b));
            const bool saturated = light_saturated(r, g, b);
            std::uint8_t table_shape = shape_slug;
            std::uint32_t texture = 0;
            const Table* t = s.table;
            const bool table_ok = t && t->rows == count && subtype < table_capacity;
            if (t && t->rows && t->rows != count) ++s.counters.stale_table;
            const std::uint8_t bits = table_ok ? t->bits[subtype] : 0;
            if (bits & row_present) table_shape = t->shape[subtype];
            if (bits & row_texture) texture = t->texture_rgb[subtype];
            style = style_from(flags, table_shape);
            if (bits & row_style_override) {
                style = std::uint8_t(t->shape[subtype] | (style & style_burst));
                eflags |= flag_override_applied;
            }
            if (bits & row_tint_override) {
                tint = t->override_rgb[subtype];
                alt = saturated ? light : texture;
                tint_source = tint_override;
                eflags |= flag_override_applied;
            } else if (saturated) {
                tint = light;
                alt = texture;
                tint_source = tint_light;
            } else if (texture) {
                tint = texture;
                alt = light;
                tint_source = tint_texture;
            } else {
                tint = 0xffffffu | rgb_present;
                alt = light;
                tint_source = tint_white;
            }
            if (bits & row_calibre_override) {
                calibre_q = t->calibre_q[subtype];
                eflags |= flag_calibre_override | flag_override_applied;
            }
            // The hull-hit class (hull-impact-classes.md section 2): the rule on the live values, then the table's pins
            // (a class pin replaces the rule's class and variant; a repair or tug pin sets its variant).
            const std::int32_t shot_shield = std::int32_t(rd32(rec + layout::bullet_shield_damage)),
                               shot_hull = std::int32_t(rd32(rec + layout::bullet_hull_damage));
            hull_class = hull_class_from(flags, shot_shield > shot_hull ? shot_shield : shot_hull, style, &variant);
            if (bits & row_hull_class) {
                hull_class = t->hull_class[subtype] < hull_class_count ? t->hull_class[subtype] : std::uint8_t(class_energy);
                variant = 0;
                eflags |= flag_override_applied;
            }
            if (bits & row_hull_repair)
                variant = detail_repair;
            else if (bits & row_hull_tug)
                variant = detail_tug;
        }
    } else {
        table = table_missiles;
        style = shape_missile_hit;
        tint = missile_tint_rgb;
        tint_source = tint_missile;
    }
    // The hit object's shield fraction and patch tier (5.1.1), for every recorded hit (the observe flight compares
    // `absorbed` with the shield state).
    const std::uint16_t hit_class = victim_is_projectile ? sc : vc;
    const std::int32_t hit_radius = std::int32_t(rd32(hit + layout::obj_radius));
    std::uint16_t shield_q = 0;
    const bool shield_ok = read_shields(hit, hit_class, s.globals, &shield_q);
    // The engine's rule, then the drained shield (drained_shield_q): an absorbed hit whose read post-hit fraction is
    // below it is a hull hit (integer compare).
    std::uint8_t role = role_from(absorbed, pclass, flags);
    const bool drained = role == role_shield_hit && shield_ok && shield_q < drained_shield_q;
    if (drained) role = role_hull_hit;
    // The stage draws a shield hit while it owned the previous frame, and with suppress_native (the default) the
    // engine's spawn is skipped for it; in shields,hull mode a hull hit likewise (suppress_native_hull; a drained shield
    // is a hull hit, weapon-look.md 3.1). A dying hit object is never the stage's: the stage retires a dying owner's
    // records unseen, so it stays native only.
    const bool shield_stage =
        stage_mode(s.mode) && s.owned_next && (s.armed & family_shields) && role == role_shield_hit && !hit_dying;
    const bool hull_stage = s.mode == mode_shields_hull && s.owned_next && s.hull_ready && (s.armed & family_hull) &&
                            role == role_hull_hit && !hit_dying;
    const bool stage = shield_stage || hull_stage;
    const bool skip = shield_stage ? s.suppress_native != 0 : hull_stage && s.suppress_native_hull != 0;
    if (hit_class == layout::class_asteroid) variant = detail_asteroid;
    std::uint8_t patch_tier = patch_fighter, tier_source = tier_by_radius;
    read_tier(hit, hit_class, hit_radius, s.globals, &patch_tier, &tier_source);
    // The projectile's travel (5.1.1 placement): its root node's row 2 (local z in world, 16.16) rotated into the hit
    // object's frame with the rows read for pos (rows still holds hit_node's), quantized; none when the node is not live
    // or the row is zero.
    const std::uintptr_t projectile_node = victim_is_projectile ? victim_node : source_node;
    std::int8_t dir[3] = {0, 0, 0};
    if (rd32(projectile_node + layout::node_live)) {
        std::int32_t row[3], local[3];
        read3(projectile_node + layout::node_rot + 32, row);
        if (row[0] | row[1] | row[2]) {
            rotate_in(rows, row, local);
            quantize_dir(local, dir);
        }
    }
    Event* e = ring_acquire(s.ring);
    if (!e) return 0;
    if (!(dir[0] | dir[1] | dir[2])) ++s.counters.dir_fallback;
    e->role = role;
    e->style = style;
    e->tier = tier_from(std::int32_t(total));
    e->flags = std::uint8_t(eflags | (absorbed == 1 ? flag_absorbed : 0) | (skip ? 0 : flag_forwarded) |
                            (nested ? flag_nested : 0) | (shield_ok ? flag_shield_read : 0) | (stage ? flag_stage : 0));
    e->table = table;
    e->tint_source = tint_source;
    e->row = subtype;
    const std::uintptr_t cfg = rd32(s.globals.cfg_slot);
    e->t0_game_ms = cfg ? rd32(cfg + layout::cfg_game_ms) : 0;
    e->lifetime_ms = role == role_hull_hit ? s.hull_lifetime_ms : s.lifetime_ms;
    e->detail = std::uint8_t((drained ? detail_drained : 0) | variant | (hull_class << detail_class_shift));
    e->calibre_q = calibre_q;
    e->tint_rgb = tint;
    e->alt_rgb332 = rgb332(alt);
    for (unsigned i = 0; i < 3; ++i) e->dir[i] = dir[i];
    e->damage = std::int32_t(total);
    e->owner = std::uint32_t(hit);
    e->node = std::uint32_t(hit_node);
    e->handle = rd32(hit_node + layout::node_handle);
    e->shield_q = shield_ok ? shield_q : 0;
    e->patch_tier = patch_tier;
    e->tier_source = tier_source;
    for (unsigned i = 0; i < 3; ++i) e->pos[i] = pos[i];
    e->radius = hit_radius;
    ++s.counters.recorded;
    if (shield_ok)
        ++s.counters.shield_read;
    else
        ++s.counters.shield_unread;
    if (nested) ++s.counters.nested;
    if (drained) ++s.counters.drained;
    if (role == role_shield_hit)
        ++s.counters.shield_hits;
    else
        ++s.counters.hull_hits;
    if (s.mode == mode_observe) {
        s.check.owner = std::uint32_t(hit);
        s.check.sequence = e->sequence;
        for (unsigned i = 0; i < 3; ++i) s.check.pos[i] = pos[i];
        if (s.check.pending) ++s.check.unmatched;
        s.check.pending = true;
    }
    if (skip)
        ++s.counters.skipped;
    else
        ++s.counters.forwarded;
    return skip ? 1 : 0;
}
// The diagnostic inner tap 0x00413da8 / 0x00413e79 -> 0x00414a70(3, eff, hit, &pos, 0), observe mode only: the
// engine's own pos for the impact just recorded, compared with the handler's (max component error). Always forwards.
static inline __attribute__((always_inline)) void tap(const std::uint32_t* esp, HookState& s) noexcept {
    ++s.counters.taps;
    if (!s.check.pending) return;
    s.check.pending = false;
    const std::uintptr_t hit = esp[3], pos = esp[4];
    if (hit != s.check.owner || !pos) {
        ++s.check.unmatched;
        return;
    }
    std::uint32_t worst = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const std::int32_t d = wrap_sub(std::int32_t(rd32(pos + 4 * i)), s.check.pos[i]);
        const std::uint32_t a = d < 0 ? std::uint32_t(0) - std::uint32_t(d) : std::uint32_t(d);
        if (a > worst) worst = a;
    }
    ++s.check.compared;
    if (worst > s.check.max_error) s.check.max_error = worst;
}

// ---------------------------------------------------------------------------------------------- Present's rules
// The ownership latch (note 2.6, review F8): the next frame's hooks skip only when this frame's stage was armed, the
// resolve completed and, when the stage's callback was set, the callback ran and succeeded (it drew, or had nothing to
// draw: S_OK / S_FALSE). A resolve that failed before the callback, a callback that never ran or returned
// DEVICELOST / an error makes the next frame's hits native.
inline bool stage_owned(bool armed, bool resolve_ok, bool callback_set, bool callback_ok) noexcept {
    return armed && resolve_ok && (!callback_set || callback_ok);
}
// The ring's scope (note 3.1): the cockpit's sector (sector_key::core::key_of, 0 unknown), the object_lifetime load
// epoch (0 unknown) and the loading state, sampled once per frame at Present. A load, a known sector key that differs
// from the held one, or a known load epoch that differs, clears the stage-owned records (their owners belong to the
// scene that went); an unknown read never clears and never replaces a held value.
struct Scope {
    std::uint64_t sector_key = 0, load_epoch = 0;
    bool loading = false;
};
struct ScopeLatch {
    std::uint64_t sector_key = 0, load_epoch = 0;
    bool changed(const Scope& now) noexcept {
        const bool sector = now.sector_key && sector_key && now.sector_key != sector_key;
        const bool epoch = now.load_epoch && load_epoch && now.load_epoch != load_epoch;
        if (now.sector_key) sector_key = now.sector_key;
        if (now.load_epoch) load_epoch = now.load_epoch;
        return now.loading || sector || epoch;
    }
};

// ---------------------------------------------------------------------------------------------- the stage (float)
// Game-time age (ms) with the clock's wrap; a t0 after now (a load reset the clock) reads as expired.
inline std::uint32_t age_ms(std::uint32_t now, std::uint32_t t0) noexcept {
    return now - t0;
}
inline bool expired(std::uint32_t now, const Event& e, std::uint32_t extra_ms = 0) noexcept {
    const std::uint32_t a = age_ms(now, e.t0_game_ms);
    return a > std::uint32_t(e.lifetime_ms) + extra_ms; // a wrapped (future) t0 is a huge age
}
// The patch look (docs/architecture/weapon-effects.md 5.1.1), the lab's controls (tools/effects/
// generate_weapon_effects_lab.py injects every value below by name). Hexagon cells of cell_m (centre to centre) fixed in
// world units per PatchTier on a stable grid in the owner's frame (six axis regions, each a planar lattice
// perpendicular to its axis, weapon_effects_stage.h); a hit lights the cell containing its projection onto its
// region's plane and its rings neighbour rings (1 + 3 rings (rings + 1) cells, drawn at the hit's depth along the
// region axis), each cell when the ring
// front from the hit cell's centre reaches its centre; a lit cell then fades over fade_ms on its own clock (a later hit
// covering it resets that clock to full, never adds; each region's cell maps of cell_map_capacity cells per draw are
// rebuilt from the live hits every frame). Per cell a deterministic stagger of brightness and fade rate (a hash of the
// cell); edges brighter than the fill; the hit's own cell is the marker (user decision 2026-11-06, replacing the
// per-pixel core flash): centre_gain x the edge and fill, its fill at centre_fill of the edge, on the same per-cell
// fade (a cell carries the level of the hit whose clock it holds: centre or ring); the ring reaches the footprint's rim
// ((rings + 0.5) cells) in 1 / ripple_speed seconds; every time is game time (SETA-scaled). The colour follows the hit
// object's shield fraction: colour_full at or above colour_high, colour_half at colour_mid, colour_empty at or below
// colour_low, smoothstep between; weapon_tint mixes the classifier's weapon colour in (an override, off).
constexpr float engine_units_per_metre = 500.f;
// Run235 Flight B (user, 2026-11-06): the cells read too small; cell_m doubled (the footprint radius, (rings + 0.5)
// cells, doubles with it).
constexpr float cell_m[patch_tier_count] = {3.0f, 6.0f, 16.0f};    // fighter, corvette, capital/station (metres)
constexpr unsigned rings[patch_tier_count] = {1, 2, 2};            // neighbour rings a hit lights about its cell
constexpr float ripple_speed = 3.333f;  // the ring front, footprint radii per game second (the rim in 0.3 s)
constexpr float ring_width = 0.12f;     // the ring's Gaussian width, footprint radii
constexpr float ring_gain = 1.0f;       // ring radiance (x the patch colour), fading as the ring nears the rim
constexpr float centre_gain = 2.0f;     // the hit cell's radiance over a ring cell's (edge and fill alike)
constexpr float centre_fill = 0.6f;     // the hit cell's fill / edge (more opaque than fill_ratio)
constexpr unsigned fade_ms = 2200;      // a lit cell's fade, game ms (the mean; x (1 +- stagger / 2) per cell)
constexpr float stagger = 0.4f;         // per-cell spread of brightness and fade rate (hash of the cell index)
constexpr float edge_width = 0.1f;      // the edge band, cells (the hexagon's inradius is 0.5)
constexpr float fill_ratio = 0.3f;      // fill radiance / edge radiance ("edge versus fill")
constexpr float cell_gain = 2.0f;       // a fresh cell's edge radiance (x the patch colour)
constexpr float colour_high = 0.75f, colour_mid = 0.5f, colour_low = 0.35f; // shield fraction bands
constexpr float colour_full[3] = {0.45f, 0.72f, 1.0f};  // light blue, linear
constexpr float colour_half[3] = {1.0f, 0.88f, 0.42f};  // light yellow
constexpr float colour_empty[3] = {1.0f, 0.4f, 0.36f};  // light red
constexpr float weapon_tint = 0.0f;     // 0 = the shield colour, 1 = the weapon's (classifier) colour; override, off
constexpr float tint_mix = 0.0f;        // the weapon colour: 0 = the classifier's, 1 = the other candidate
constexpr float soft = 1.0f;            // the decals' two-sided fade against the lane, cells (the patches lie on the
                                        // lane itself)
// Run236 Flight C (user, 2026-11-06): in the cockpit view the own ship's patch sits around the eye and covers the view.
// Its patches (owner node = the own-ship root node) draw at own_cockpit_gain of their radiance while the view is the
// cockpit (view mode 1); the chase view and every other ship are unchanged (stage::dim_own_cockpit).
constexpr float own_cockpit_gain = 0.35f;
// Run240 Flight G (user, 2026-11-06): in the cockpit the own ship's patch read "cut at the screen's horizontal centre"
// (the eye inside the hull-centred bubble: the lit far side of hits on the lower nose lies at or below the eye's
// horizontal plane, which projects to the centre line). In the cockpit view the own ship's bubble is a sphere centred on
// the eye, radius its smallest bubble half-extent (inside the hull extents) and never under eye_sphere_min_m; each hit
// maps to it along its direction from the eye; its far side draws (the eye at the centre: one far root per ray).
constexpr float eye_sphere_min_m = 15.0f;
// The screen-space patch (Run236 Flight C, user decision 2026-11-06): each pixel's face is chosen by its surface normal
// (from the lane depth), blended over the normal's owner-frame components |n_k|^triplanar_sharpness, normalised.
constexpr float triplanar_sharpness = 8.0f;
// Less a floor, renormalised: a face past about 48 degrees of tilt takes no share (its pixels would depend on which hit
// anchors a cell, which moves along a steep face: the capital sphere's pops in the fixture without it).
constexpr float triplanar_floor = 0.3f;
// The face cull's cut (stage::face_culled): with the floor f at sharpness s a face takes a share only while
// tan^s(tilt) < (1 - f) 2^(s/2 - 1) / f (the rest of the normal split evenly over the two other axes, its least
// weight), 18.67 at s = 8, f = 0.3: tilt < 55.26 degrees; with 2 degrees of margin a box whose axis points away from the
// eye by more than 90 + 57.26 degrees at every corner is culled: sin 57.26 deg = 0.841.
constexpr float face_cull_sin = 0.841f;
// The frame's screen-area budget (review F4): the drawn boxes' projected areas summed, in screens; past it the draws of
// the owners whose newest hit is the oldest are dropped (area_dropped), so a frame's patch cost is bounded by about
// this many full-screen boxes.
constexpr float screen_budget = 8.0f;
// The ship bubble (user decision after Run238 Flight E, 5.1.1 "Placement"): an owner whose tier is not the station's
// (TierSource tier_by_station keeps the hull decal) gets a smooth ellipsoid in its frame: centre the hull bounds' centre,
// half-extents the hull bounds' half-extents x bubble_scale (the hull_children walk of render-node-bounds.md 6.1,
// weapon_effects_bubble.h), each floored at bubble_floor of the largest and cut to bubble_scale x the object radius; a failed walk
// takes a sphere of the object radius about the root node (bubble_fallback=). Per pixel the ray meets it twice: the near side draws at full
// radiance, the far side (seen around the hull's outline, the hull lane hiding what lies behind it) at
// bubble_far_gain; both add where both draw. A failed walk is retried after bubble_retry_ms of game time.
constexpr float bubble_scale = 1.15f;
constexpr float bubble_far_gain = 0.6f;
constexpr float bubble_floor = 1.f / 16.f;
constexpr unsigned bubble_retry_ms = 2000;
// At most this many hull walks per stage call (review F4: a crowded sector's first frame); the owners past it draw on
// the fallback sphere until a later frame walks them (bubble_deferred= in weapon_effects_frame).
constexpr unsigned bubble_walks_per_frame = 2;
// The records' game-time life: the ring reaches the rim, then the slowest cell fades out.
constexpr unsigned patch_lifetime_ms = unsigned(1000.f / ripple_speed + float(fade_ms) * (1.f + 0.5f * stagger)) + 1;
static_assert(patch_lifetime_ms > 0 && patch_lifetime_ms < 65535, "the record's lifetime is a u16 of game ms");
// Engine integers to render (view) units: the gameplay context scale (own_sun_core.h context_scale).
constexpr float world_scale = 0.01f;
constexpr unsigned max_owners = 16;   // owners drawn per frame
constexpr unsigned max_patches = 64;  // patch draws per frame (one per owner and lit face not culled, and spills)
constexpr unsigned max_decals = 16;   // flat decals per frame (hits on an owner without a usable ellipsoid)
constexpr unsigned hit_slots = 4;     // ring and core hits per draw (the region's newest hits still in their ring)
constexpr unsigned owner_hits = 64;   // live hits per owner feeding its cell map (newest first)
constexpr unsigned cell_map_capacity = 128; // lit cells per draw (one owner and face; a face spills into more)
constexpr unsigned disarm_frames = 64;

// ---------------------------------------------------------------------------------------------- the shared look
// docs/architecture/weapon-look.md 2.1-2.11 and 5: the language every family after the shield hit draws in. Radiance is
// linear, engine space, before the TAA and AgX; sizes of a target in its cell unit U = cell_m[tier] scaled by the
// calibre k; every life in game ms with the three-frame floor. The lab reads these by name (the generator's namespace-
// scoped reader) and the fixture re-pins them.
namespace look {
// The heat ramp's five stops by temperature T = 0, 0.25, 0.5, 0.75, 1 (black, red, orange, yellow, white; linear);
// radiance = ramp(T) x E x T, so a cooling element darkens and saturates together.
constexpr float heat_ramp[5][3] = {{0.f, 0.f, 0.f}, {0.55f, 0.10f, 0.03f}, {1.0f, 0.42f, 0.10f}, {1.0f, 0.72f, 0.30f},
                                   {1.0f, 0.92f, 0.80f}};
constexpr unsigned min_frames = 3;      // no element lives fewer frames: life = max(life, min_frames x game step)
constexpr float soft_core = 0.15f;      // volumetric fade against the lane, x the element's radius: cores and sparks
constexpr float soft_halo = 1.0f;       // halos (and later fireballs and shock shells)
constexpr float core_e = 4.0f;          // a core's radiance (may clip white)
constexpr float body_max = 1.5f;        // a body or frame-filling element never above this
constexpr float halo_max = 0.2f;        // a halo's peak over its core's, e-folded within halo_fold core radii
constexpr float halo_fold = 1.5f;
constexpr float streak_cap = 6.0f;      // a streak at most this many rest lengths (its radiance x rest / (rest + streak))
constexpr float streak_core_min = 1.2f; // ... but a diluted core never below this (it survives the resolve)
constexpr float floor_core_px = 2.0f, floor_lit_px = 3.0f, floor_len_px = 24.0f, floor_flash_px = 3.0f; // pixel floors
// floor_len_px: a streak (the hull sparks) never shorter than 24 px (12 until 2026-11-06: the lab's sparks unseen).
// The shared screen budget's family weights (2.11): a draw is charged its projected area in screens x its weight (the
// family's full-screen us over the decal's 177.5 us at 5120x1440). The glow: 71.4 us for one rectangle filling the
// screen at 5120x1440 (native fixture, glow_alone_fills_frame, 2026-11-06; four runs 71.0-72.3) / 177.5 = 0.40 (the
// note's [I] was 0.25).
constexpr float budget_weight_patch = 1.0f, budget_weight_glow = 0.40f;
// The fireball impostor (2.14; the hull hit's hot-gas burst since 2026-11-06, the explosions of step 3 at full size): a
// ray-sphere impostor whose column density is the closed-form chord integral of rho(r) = 1 - r^2 / R^2, clipped at the
// lane (the half inside a hull is not there), times two octaves of value noise sampled at the chord's middle in the
// sphere's normalised frame and advected outward with age (fireball_advect noise cells over the life), eroded as it
// cools; its heat T x (1 - fireball_core_t (1 - column)) on the heat ramp, radiance ramp(T) x E x T x density.
// fireball_octaves is the program's octave count (the budget's LOD drops it to 1, 2.11 (1)).
constexpr float fireball_freq = 2.2f;     // noise cells per sphere radius
constexpr float fireball_advect = 1.5f;   // outward drift of the pattern over the life (noise cells)
constexpr float fireball_noise = 0.9f;    // the density's noise amplitude (1 +- this)
constexpr float fireball_erode = 0.6f;    // the density lost at the end of the life (the ball breaks up as it cools)
constexpr float fireball_core_t = 0.35f;  // the rim's heat below the core's
constexpr unsigned fireball_octaves = 2;
// One fireball rectangle filling the frame at 5120x1440, EVENT-fenced (native fixture, burst_alone_fills_frame,
// 2026-11-06: 184.3 us at two octaves, 135.4 at one), over the decal's 177.5 us.
constexpr float budget_weight_fireball = 1.04f, budget_weight_fireball_lod = 0.76f;
} // namespace look
// The hull hit (weapon-look.md 3.1): a 60 ms white flash, isotropic cooling sparks, a 150 ms glow on the hull and a
// 1.2 s ember, all at the recorded contact point; r_hit = r_scale U (r_base + r_k k). mode full | simple (the kill fallback:
// flash + glow + ember, no sparks) is a header constant, never a setting.
namespace hull {
enum Mode : std::uint8_t { full = 0, simple = 1 };
constexpr Mode mode = full;
// The kill criteria's other switches (3.1): (a) the spark count halves once (spark_count_x 0.5); (b) a glow seen on the
// wrong ship or in space drops the glow (glow false; flash, sparks and ember remain); (c) the own nose hit unseen from
// the chase camera: glow_r and glow_e x 2, spark_speed_u x 1.5, once.
constexpr float spark_count_x = 1.0f;
constexpr bool glow = true;
constexpr float r_base = 0.35f, r_k = 0.65f;
// r_hit's scale (2026-11-06, user: as the shield cells and the plumes, too small on ships tens of metres long): the
// flash, glow and ember scale with it; the sparks' reach and width stay in U.
constexpr float r_scale = 2.0f;
// The per-class anatomy (docs/architecture/hull-impact-classes.md section 4; ratified 2026-11-06): one row per Anatomy
// (the six HullClass values, then the missile source, the asteroid victim, repair and tug). Sizes over r = r_hit (or U
// where named), lives in game ms (x (1 +- stagger / 2) by hash, the three-frame floor). The lab reads every grid by name.
enum Anatomy : std::uint8_t { an_energy = 0, an_ballistic = 1, an_plasma = 2, an_heavy = 3, an_ion = 4, an_beam = 5,
                              an_missile = 6, an_asteroid = 7, an_repair = 8, an_tug = 9, anatomy_count = 10 };
// Flash: core r, halo r, life ms, halo heat, halo colour (0 heat, 1 the tint when saturated (plasma), 2 ion_colour (fixed,
// ratified 2026-11-06), 3 the missile tint). A zero life: no flash. The heavy class adds the entry streak (streak_*).
constexpr float class_flash[10][5] = {{0.25f, 1.0f, 60.f, 0.9f, 0.f}, {0.15f, 0.4f, 40.f, 1.0f, 0.f},
                                      {0.35f, 2.0f, 60.f, 1.0f, 1.f}, {0.2f, 0.6f, 60.f, 0.9f, 0.f},
                                      {0.2f, 1.0f, 60.f, 1.0f, 2.f}, {0.25f, 1.0f, 60.f, 0.9f, 0.f},
                                      {0.25f, 1.0f, 60.f, 0.9f, 3.f}, {0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f, 0.f}};
// Puff (the fireball impostor riding the owner): R_b = rb_r r + U (rb_u + rb_uk k), L = life + life_k k ms, heat T0 -> 0
// over L, body E (<= look::body_max), R(t) = R_b (1 - e^(-t / (rise L))) x (1 - shrink max(0, (t / L - 0.3) / 0.7)), the
// tint mixed in at tint_mix (1 - t / L) when saturated (plasma). Columns: rb_r, rb_u, rb_uk, life, life_k, T0, E, rise,
// shrink, tint_mix. A zero radius: no puff.
constexpr float class_puff[10][10] = {{1.0f, 0.f, 0.f, 350.f, 150.f, 1.0f, 1.5f, 0.15f, 0.f, 0.f},
                                      {0.4f, 0.f, 0.f, 200.f, 0.f, 0.6f, 0.8f, 0.15f, 0.f, 0.f},
                                      {1.5f, 0.f, 0.f, 600.f, 400.f, 1.0f, 1.5f, 0.2f, 0.5f, 0.5f},
                                      {0.8f, 0.f, 0.f, 300.f, 200.f, 1.0f, 1.5f, 0.1f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.6f, 0.f, 0.f, 300.f, 0.f, 1.0f, 1.5f, 0.15f, 0.f, 0.f},
                                      {0.f, 1.0f, 1.0f, 500.f, 700.f, 1.0f, 1.5f, 0.15f, 0.f, 0.f},
                                      {1.2f, 0.f, 0.f, 800.f, 400.f, 0.3f, 0.5f, 0.15f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}};
// Spark sets, three per anatomy (row 3 a + set: 0 fast, 1 slow, 2 chips): n = n0 + nk k (x the count's spread), speed U/s
// (1 +- speed_spread), life + life_k k ms (x the spark's factor), width U, rest length in widths, tau s, radiance E at
// birth (x its life fraction f), heat T0 f, the shot-axis bias b (v = speed normalize(u + b a), a = Event::dir in world;
// b 0 isotropic, and without dir), duty: the share of flicker_ms slots it is lit in (0: always). Columns: n0, nk, speed,
// speed_spread, life, life_k, width, rest_w, tau, E, T0, bias, duty. n0 = nk = 0: no set. The beam's slow row is its
// spatter: n0 sparks per spatter_period_ms while held. The ion's sparks take ion_colour.
constexpr float class_sparks[30][13] = {
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // energy: the spatter (slow) only
    {4.f, 4.f, 3.f, 0.3f, 150.f, 100.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {8.f, 12.f, 8.f, 0.3f, 150.f, 50.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, -0.6f, 0.f}, // ballistic: splash back, two lives
    {4.f, 6.f, 4.f, 0.3f, 450.f, 150.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, -0.3f, 0.f},
    {3.f, 3.f, 2.f, 0.5f, 600.f, 200.f, 0.12f, 1.5f, 1.0f, 1.5f, 0.3f, -0.3f, 0.f}, // chips
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // plasma
    {3.f, 5.f, 3.f, 0.4f, 400.f, 200.f, 0.10f, 3.f, 0.45f, 8.f, 1.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {12.f, 12.f, 10.f, 0.4f, 300.f, 200.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, -2.0f, 0.f}, // heavy: the back-jet
    {6.f, 6.f, 12.f, 0.3f, 400.f, 200.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, 2.0f, 0.f}, // the exit jet
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {4.f, 4.f, 6.f, 0.5f, 120.f, 0.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, 0.f, 0.5f}, // ion: the arc sparks, flickering
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // beam: the spatter (slow)
    {2.f, 0.f, 4.f, 0.4f, 300.f, 0.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {16.f, 24.f, 8.f, 0.3f, 150.f, 50.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, -0.3f, 0.f}, // missile: two debris sets
    {8.f, 12.f, 3.f, 0.3f, 1500.f, 0.f, 0.08f, 3.f, 1.0f, 8.f, 1.f, -0.3f, 0.3f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // asteroid: rock dust and chips
    {8.f, 8.f, 3.f, 0.3f, 450.f, 150.f, 0.08f, 3.f, 0.45f, 1.5f, 0.3f, 0.f, 0.f},
    {6.f, 6.f, 2.f, 0.5f, 600.f, 200.f, 0.12f, 1.5f, 1.0f, 1.5f, 0.25f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // repair
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // tug
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}};
// Glow (the glow primitive at the hit): R r, life ms (a beam's and repair's: the fall after the hold), heat T, radiance
// E (repair: the weapon tint, tug: tug_colour, at E). Ember (the post-hit light): R r, life + life_k k ms, heat T0
// cooling to 0, E (radiance ramp(T) x E x T; repair: the tint x E fading); the beam's is its spots' trail ember.
constexpr float class_glow[10][4] = {{3.f, 150.f, 0.85f, 1.0f}, {1.5f, 100.f, 0.9f, 0.6f}, {4.f, 250.f, 0.8f, 1.2f},
                                     {3.f, 200.f, 0.9f, 1.5f},  {0.f, 0.f, 0.f, 0.f},       {2.5f, 150.f, 0.85f, 1.0f},
                                     {4.f, 250.f, 0.85f, 1.5f}, {0.f, 0.f, 0.f, 0.f},       {2.f, 150.f, 0.f, 1.0f},
                                     {2.f, 150.f, 0.f, 1.0f}};
constexpr float class_ember[10][5] = {{0.8f, 1500.f, 500.f, 0.6f, 0.8f}, {0.3f, 500.f, 0.f, 0.4f, 0.5f},
                                      {1.0f, 3000.f, 0.f, 0.6f, 1.0f},   {0.5f, 2000.f, 0.f, 0.7f, 1.2f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f},         {0.8f, 1500.f, 0.f, 0.6f, 0.8f},
                                      {1.0f, 2000.f, 0.f, 0.6f, 1.0f},   {0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.4f, 800.f, 0.f, 0.f, 0.6f},     {0.f, 0.f, 0.f, 0.f, 0.f}};
// Surface ring (the glow primitive with a shell profile, weapon_ring_ps.hlsl): E = heat(ring_t) x E x exp(-((d - r(t)) /
// w)^2) x (1 - t / L)^2, r(t) = r_max (1 - e^(-t / (ring_rise L))), d the lane point's distance from the hit: the sphere
// shell cut by whatever surface the lane holds, no normal. Columns: r_max r, w r, L ms, E. A zero radius: none.
constexpr float class_ring[10][4] = {{0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}, {2.5f, 0.15f, 300.f, 1.0f},
                                     {3.f, 0.12f, 250.f, 1.2f}, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f},
                                     {3.f, 0.15f, 300.f, 1.0f}, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f},
                                     {0.f, 0.f, 0.f, 0.f}};
constexpr float ring_t = 0.9f, ring_rise = 0.1f;
// The shock front (2026-11-07, hull-impact-classes.md 3.5; the lab's renderHull draws the same law): with d' the
// stretched distance |M d| (M: the owner's axes, x 1 / ring_stretch along the shot's heading when it is valid) and
// z = (d' - r) / w, E = colour(ring_lead_t) x E0 x (m (exp(-z^2) + 4 b (1 - b) trail) + wash), b = saturate(-z /
// ring_trail_span) (the trailing band from r - ring_trail_span w to r), trail = ring_trail_gain heat(ring_trail_t) /
// heat(ring_lead_t); m = 1 - depth + depth saturate(0.5 + contrast (n - 0.5)), n the value noise of the owner-frame
// direction x ring_noise_freq + 64 unit(hash(seq, 7)) (1.7, 3.1, 0.7) (bright and weak arcs, stable per hit);
// wash = ring_wash_gain saturate((r - d') / w) e^(-(r - d') / (ring_wash_fall r)) inside the front only. The plasma
// and heavy hits' shimmer rects carry the ring (ring_refract x (1 - t / L)^2): a displacement band one
// engine_shimmer::weapon_ring_band_px inside the front.
constexpr float ring_lead_t = 1.0f, ring_trail_t = 0.35f, ring_trail_gain = 0.55f, ring_trail_span = 3.0f;
constexpr float ring_noise_freq = 1.3f, ring_noise_depth = 0.9f, ring_noise_contrast = 2.5f;
constexpr float ring_wash_gain = 0.25f, ring_wash_fall = 0.35f, ring_stretch = 1.25f, ring_refract = 1.0f;
// The noise's seed (64 unit(hash(seq, 7)) x (1.7, 3.1, 0.7)) and the profile's windows in w: the noise where
// -span < z < ring_noise_window, the front (edge, band, noise) where -span < z < ring_front_window, the box's edge.
constexpr float ring_seed_axis[3] = {1.7f, 3.1f, 0.7f};
constexpr float ring_seed_scale = 64.f, ring_noise_window = 2.5f, ring_front_window = 3.f;
constexpr unsigned ring_seed_element = 7;
// Scorch (the multiply decal, the only non-additive element): the glow's window drawn ZERO / INVSRCCOLOR, output s(t)
// w(d) scorch_colour, s(t) = s0 min(1, t / L_ember) (1 - t / L_s): it develops while the ember cools and fades linearly;
// in its own per-owner store (scorch_per_owner, scorch_store), drawn first, never on sky. Columns: R r, L_s ms, s0.
constexpr bool scorch = true; // the kill switch (section 9: false keeps every other element)
constexpr float class_scorch[10][3] = {{0.6f, 12000.f, 0.45f}, {0.3f, 10000.f, 0.5f}, {1.2f, 20000.f, 0.6f},
                                       {0.6f, 15000.f, 0.7f},  {0.f, 0.f, 0.f},         {0.8f, 15000.f, 0.5f},
                                       {1.5f, 20000.f, 0.6f},  {0.f, 0.f, 0.f},         {0.f, 0.f, 0.f},
                                       {0.f, 0.f, 0.f}};
constexpr float scorch_colour[3] = {0.75f, 0.80f, 0.85f};
constexpr unsigned scorch_per_owner = 16, scorch_store = 128;
// Heat shimmer rects (plasma, heavy): one oriented square of shimmer_r r about the hit along the shot's projection (the
// view's x without dir), strength 1 - t / shimmer_ms, at most shimmer_max a frame after the engines' rects.
constexpr float class_shimmer[10] = {0.f, 0.f, 3.f, 3.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
constexpr unsigned shimmer_ms = 400, shimmer_max = 2;
// Heavy: the entry streak from P - streak_back r a to P + streak_front r a, width streak_width r, white at streak_e,
// streak_ms; without dir the flash at streak_fallback_halo r instead.
constexpr float streak_back = 1.5f, streak_front = 0.5f, streak_width = 0.3f, streak_e = 4.0f, streak_fallback_halo = 1.5f;
constexpr unsigned streak_ms = 60;
// Ion: the filament glow (weapon_filament_ps.hlsl): R filament_r r, E filament_e x (1 - t / filament_ms), the noise at
// filament_scale / r in the owner's frame re-hashed every rehash_ms, lit where |n| < filament_width; ion_colour.
constexpr float filament_r = 1.5f, filament_e = 2.0f, filament_scale = 6.0f, filament_width = 0.08f;
constexpr unsigned filament_ms = 500, rehash_ms = 100, flicker_ms = 40;
constexpr float ion_colour[3] = {0.60f, 0.75f, 1.00f};
// Beam: the hold rule (section 3.9): a new beam record of a held spot's owner and (table, row) within hold_r r re-lights
// it; held while now - t_last <= max(hold_ms, 2 game steps), the spot core spot_core / spot_halo r falling over
// spot_fall_ms after, the boiling puff and the glow over their own lives; spatter n0 sparks per spatter_period_ms while
// held (at most spatter_cap live); a scorch mark every trail_step_r r of the spot's travel (the trail ember the spot's
// own, from its fall).
constexpr unsigned hold_ms = 60, spot_fall_ms = 80, spatter_period_ms = 40, spatter_cap = 24, max_spots = 64;
constexpr float hold_r = 0.5f, spot_core = 0.2f, spot_halo = 0.6f, trail_step_r = 0.5f;
constexpr float flash_halo_t = 0.9f; // the default halo heat
// The sparks' shared spreads (2026-11-06, the user: "sparks read as the same decals appearing and disappearing"): per hit
// the count round(n x (1 + spark_count_spread (2 u - 1))) (u hashed from the record's sequence), in antipodal pairs of
// hashed directions (the lane hides the half that flies into the hull); per spark its speed x (1 +- the set's spread),
// rest length x [spark_len_lo, spark_len_hi], width x [spark_width_lo, spark_width_hi] and life x [spark_life_lo,
// spark_life_hi]; spark_curl_share of them drift sideways at spark_curl_x of their own speed. A hit is the same frame to
// frame.
constexpr float spark_count_spread = 0.4f;
constexpr float spark_life_lo = 0.7f, spark_life_hi = 1.3f;
constexpr float spark_len_lo = 0.6f, spark_len_hi = 1.6f;     // x the rest length (after its floors)
constexpr float spark_width_lo = 0.8f, spark_width_hi = 1.2f; // x the width (before floor_lit_px)
constexpr float spark_curl_share = 0.25f, spark_curl_x = 0.3f;
constexpr float spark_rest_w = 3.0f; // the streak program's rest length in widths (a set's rest_w scales its len_x)
constexpr float bias = 0.5f;         // the flash's centre towards the eye, x r_hit (its own surface does not halve it)
constexpr float tug_colour[3] = {0.7f, 0.8f, 1.0f}; // the tug's glow (pale blue); repair: the weapon tint
// The shared budget's weights of the new primitives (2.11, over the decal's 177.5 us at 5120x1440; the native fixture's
// *_alone_fills_frame rows, 2026-11-06: the ring 71.6 us, the filament glow 153.2, the scorch 86.7, a flash's halo
// holding the eye 87.3; the ring as a shock front 88.1-90.5 us, 2026-11-07): charged; the flashes are the shed order's
// last step (other owners', oldest first).
constexpr float budget_weight_ring = 0.50f, budget_weight_filament = 0.86f, budget_weight_scorch = 0.49f;
constexpr float budget_weight_flash = 0.49f;
// A kept hull shimmer rect (with its ring's band) per screen of its drawn rect over the decal's 177.5 us per screen:
// run_engine_shimmer.py --host native 2026-11-07, two rings at the cap's reach (WEAPON_RING_TIMING) 245.4 us for 0.486
// screens of boxes at 5120x1440 (2.84; a plume rect of a tenth 44.2 us, 2.49).
constexpr float budget_weight_shimmer = 2.85f;
// The puffs' octave LOD at fighter range (section 6): a puff whose projected radius is under puff_lod_px (at 1440 rows,
// scaled with the target's height) draws one noise octave (the sign of its seed), charged at budget_weight_fireball_lod.
constexpr float puff_lod_px = 96.f;
constexpr unsigned max_sparks = 2048;   // spark quads per frame (2.11)
constexpr unsigned max_owners = 32;     // owners re-posed per frame
} // namespace hull
static_assert(max_owners + hull::max_owners <= VisibleOwners::capacity, "every owner a stage frame can draw");
// The view volume's sphere about a shield stage owner (stage::owner_in_view): its object radius (obj+0xa4) x this, past
// the bubble (bubble_scale), so an owner just past the screen edge whose bubble still reaches into it counts as drawn.
// The hull stage judges its owners by their reach sphere instead (stage::out_of_reach: 2 r + the records' element reach,
// grown by the pixel floors), the same test its cull uses.
constexpr float visible_radius_scale = 2.0f;
// A hull hit's record life per anatomy (review 2026-11-07): its slowest element (an ember, a puff, a glow, a ring, the
// flash, the streak, the filament glow at k 1 x (1 + stagger / 2), a spark set at spark_life_hi; the scorches live in
// their own store, the beam, repair and tug spots in theirs), so each class leaves the ring at its own end;
// hull_lifetime_ms the slowest of them (the installed bound). Present keeps a record life_floor_ms longer. Compile-time
// integers: the handler reads the table, never a float.
constexpr float hull_slowest_ms(unsigned a) noexcept {
    const float st = 1.f + 0.5f * stagger;
    float m = 0.f;
    const float cand[7] = {(hull::class_ember[a][1] + hull::class_ember[a][2]) * st,
                           (hull::class_puff[a][3] + hull::class_puff[a][4]) * st, hull::class_glow[a][1] * st,
                           hull::class_ring[a][2] * st, hull::class_flash[a][2] * st,
                           a == hull::an_heavy ? float(hull::streak_ms) * st : 0.f,
                           a == hull::an_ion ? float(hull::filament_ms) * st : 0.f};
    for (float c : cand) m = c > m ? c : m;
    for (unsigned k = 0; k < 3; ++k) {
        const float sl = (hull::class_sparks[3 * a + k][4] + hull::class_sparks[3 * a + k][5]) * hull::spark_life_hi;
        m = sl > m ? sl : m;
    }
    return m;
}
struct HullLives {
    std::uint16_t v[hull::anatomy_count];
};
constexpr HullLives make_hull_lives() noexcept {
    HullLives l{};
    for (unsigned a = 0; a < hull::anatomy_count; ++a) l.v[a] = std::uint16_t(unsigned(hull_slowest_ms(a)) + 1);
    return l;
}
constexpr HullLives hull_lives = make_hull_lives();
constexpr unsigned hull_lifetime_max() noexcept {
    unsigned m = 0;
    for (unsigned a = 0; a < hull::anatomy_count; ++a) m = hull_lives.v[a] > m ? hull_lives.v[a] : m;
    return m;
}
constexpr unsigned hull_lifetime_ms = hull_lifetime_max();
static_assert(hull_lifetime_ms > 0 && hull_lifetime_ms < 65535, "the record's lifetime is a u16 of game ms");
// The heat ramp at T (clamped to [0, 1]), linear between its stops.
inline void heat_colour(float t, float out[3]) noexcept {
    t = t > 0.f ? (t < 1.f ? t : 1.f) : 0.f;
    const float x = t * 4.f;
    unsigned i = unsigned(x);
    if (i > 3) i = 3;
    const float f = x - float(i);
    for (unsigned c = 0; c < 3; ++c) out[c] = look::heat_ramp[i][c] + (look::heat_ramp[i + 1][c] - look::heat_ramp[i][c]) * f;
}
// e^x for x <= 0 without libm (SSE2 build: no x87): 2^(x log2 e), the integer part in the exponent, a degree-5 fit of
// 2^f on [0, 1) (relative error below 2e-6); 0 below -80.
inline float exp_neg(float x) noexcept {
    if (!(x < 0.f)) return 1.f;
    if (x < -80.f) return 0.f;
    const float y = x * 1.44269504f; // log2 e
    int i = int(y);
    if (float(i) > y) --i;
    const float f = y - float(i);
    const float p = 1.f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    std::uint32_t b = std::uint32_t(i + 127) << 23;
    float s;
    std::memcpy(&s, &b, 4);
    return p * s;
}

// log10 without libm (SSE2 build: no x87): the float's exponent plus ln(m) = 2 atanh((m - 1) / (m + 1)).
inline float log10f_sse(float x) noexcept {
    if (!(x > 0.f)) return -1e30f;
    std::uint32_t b;
    std::memcpy(&b, &x, 4);
    const int e = int((b >> 23) & 0xff) - 127;
    b = (b & 0x7fffffu) | 0x3f800000u;
    float m;
    std::memcpy(&m, &b, 4);
    const float z = (m - 1.f) / (m + 1.f), z2 = z * z;
    const float ln_m = 2.f * z * (1.f + z2 * (1.f / 3.f + z2 * (1.f / 5.f + z2 * (1.f / 7.f + z2 * (1.f / 9.f)))));
    return (float(e) * 0.69314718f + ln_m) * 0.43429448f;
}
inline float calibre_of(std::int32_t d) noexcept {
    if (d <= 0) return 0.f;
    const float k = log10f_sse(float(d) / calibre_reference) / calibre_decades;
    return k < 0.f ? 0.f : k > 1.f ? 1.f : k;
}
inline float calibre_of(const Event& e) noexcept {
    return (e.flags & flag_calibre_override) ? float(e.calibre_q) * (1.f / 255.f) : calibre_of(e.damage);
}
// The shield fraction of a record: the hook's read, or full shields (light blue) when it was refused.
inline float shield_fraction(const Event& e) noexcept {
    return (e.flags & flag_shield_read) ? float(e.shield_q) * (1.f / 65535.f) : 1.f;
}
struct ShieldBands {
    float high = colour_high, mid = colour_mid, low = colour_low;
    float full[3] = {colour_full[0], colour_full[1], colour_full[2]};
    float half[3] = {colour_half[0], colour_half[1], colour_half[2]};
    float empty[3] = {colour_empty[0], colour_empty[1], colour_empty[2]};
};
// The patch colour of a shield fraction (5.1.1): full at or above `high`, half at `mid`, empty at or below `low`, a
// smoothstep between (C1 at every band edge).
inline void shield_colour(float f, const ShieldBands& b, float out[3]) noexcept {
    if (!(f == f)) f = 1.f;
    const float* from = b.half;
    const float* to = b.full;
    float t = 1.f;
    if (f <= b.low) {
        t = 0.f;
        from = to = b.empty;
    } else if (f < b.mid) {
        const float span = b.mid - b.low;
        t = span > 1e-6f ? (f - b.low) / span : 1.f;
        from = b.empty;
        to = b.half;
    } else if (f < b.high) {
        const float span = b.high - b.mid;
        t = span > 1e-6f ? (f - b.mid) / span : 1.f;
    }
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    t = t * t * (3.f - 2.f * t);
    for (unsigned i = 0; i < 3; ++i) out[i] = from[i] + (to[i] - from[i]) * t;
}
// sRGB 8-bit colour word to linear, normalised so the largest channel is 1 (white for an absent or black word).
inline float srgb8_to_linear(std::uint32_t c) noexcept {
    const float v = float(c) * (1.f / 255.f);
    return v <= 0.04045f ? v / 12.92f : (v * v) * (0.7f + 0.3f * v); // a cheap fit of the EOTF (error below 0.01)
}
inline void tint_linear(std::uint32_t word, float out[3]) noexcept {
    if (!(word & rgb_present)) {
        out[0] = out[1] = out[2] = 1.f;
        return;
    }
    float m = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        out[i] = srgb8_to_linear((word >> (8 * i)) & 0xff);
        if (out[i] > m) m = out[i];
    }
    for (unsigned i = 0; i < 3; ++i) out[i] = m > 0.f ? out[i] / m : 1.f;
}
// The narrowed alt candidate back to a colour word (each channel's top bits, scaled to 0..255).
inline std::uint32_t rgb332_word(std::uint8_t q) noexcept {
    if (!q) return 0;
    const std::uint32_t r = (q >> 5) & 7u, g = (q >> 2) & 7u, b = q & 3u;
    return pack_rgb(r * 255u / 7u, g * 255u / 7u, b * 255u / 3u);
}
inline void event_tint(const Event& e, float mix, float out[3]) noexcept {
    tint_linear(e.tint_rgb, out);
    const std::uint32_t other = rgb332_word(e.alt_rgb332);
    if (!(other & rgb_present) || !(mix > 0.f)) return;
    float alt[3];
    tint_linear(other, alt);
    const float t = mix > 1.f ? 1.f : mix;
    for (unsigned i = 0; i < 3; ++i) out[i] += (alt[i] - out[i]) * t;
}
} // namespace x3m::weapon_effects::core
