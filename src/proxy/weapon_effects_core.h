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
// A death record's own uses of the impact-only bits: flag_swapped (the native spawn ran through the clone row R', with
// flag_stage and flag_forwarded), and the stage's answer to unknown 4 (explosion-look.md section 11): whether it looked
// up the dying owner in the bubble cache (flag_bubble_looked) and found its box there (flag_bubble_cached).
constexpr std::uint8_t flag_swapped = flag_absorbed, flag_bubble_looked = flag_override_applied,
                       flag_bubble_cached = flag_calibre_override;
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
// The death sites (spawn note 4 and 8.1): the class 0x1c the callee refuses at once, the missile's area flag
// (obj+0x40 & 0x1000000: with a non-zero killer the callee returns 1 with no explosion, rule (b)), and the per-class
// death-row columns of the type records (TShips +0xbc col 47, TDocks / TFactories +0x60 col 14, TAsteroids +0x48 col 8,
// TMissiles +0x7c col 21).
constexpr std::uint16_t class_no_effect = 0x1c;
constexpr std::uint32_t obj_missile_area = 0x1000000u;
constexpr unsigned ship_death_row = 0xbc, station_death_row = 0x60, asteroid_death_row = 0x48, missile_death_row = 0x7c;
constexpr unsigned caller_reason = 8; // the kill routine 0x0044abd0's word [ebp+8]: the reason
// The Effects row table (spawn note 10.1: 0x00580068 + id 0x14, ids 0..999; the loader 0x004126d0): count +0, EDF flags
// +4 (|1 for a BREAKUP element, |2 EDF_PROPAGATE for a PROPAGATE one: the death path's second carrier at 0x004138fa),
// +8 / +0xc copied, the element array +0x10 (count 0x70-byte records). Element fields (10.2): body +0, I3DF +4, timeout
// +0x10, EEDF flags +0x3c, start +0x40. EEDF bits: TIMEOUT 0x10, CUT 0x20, LIGHT 0x80, FADEOUTBODY 0x800 (the carrier's
// root faded over a fixed 1000 ms from the element's start), PROPAGATE 0x4000, DISTANT 0x20000 (phase 2 at its start:
// never a node), WRECK 0x4000000.
constexpr unsigned row_stride = 0x14, row_count = 0, row_flags = 4, row_8 = 8, row_c = 0xc, row_elements = 0x10;
constexpr unsigned row_ids = 1000, element_stride = 0x70;
constexpr unsigned el_body = 0, el_i3df = 4, el_timeout = 0x10, el_flags = 0x3c, el_start = 0x40;
constexpr std::uint32_t edf_propagate = 2;
constexpr std::uint32_t eedf_timeout = 0x10, eedf_fadeoutbody = 0x800, eedf_propagate = 0x4000, eedf_distant = 0x20000,
                        eedf_wreck = 0x4000000;
}
struct Globals {                     // the engine's slots (X3AP.exe VAs in production; the fixture maps the same pages)
    std::uintptr_t cfg_slot = 0x00606f34;
    std::uintptr_t bullet_table_slot = 0x00606fb8, bullet_count_slot = 0x00607038;
    std::uintptr_t missile_table_slot = 0x00606fe0, missile_count_slot = 0x00607060;
    std::uintptr_t ship_table_slot = 0x00606fd4, ship_count_slot = 0x00607054;
    std::uintptr_t shield_table_slot = 0x00606fdc, shield_count_slot = 0x0060705c;
    // Step 3 (the death sites' row read): TDocks 5, TFactories 6, TAsteroids 17 (0x00606fb8 + 4 class, counts
    // 0x00607038 + 4 class).
    std::uintptr_t dock_table_slot = 0x00606fcc, dock_count_slot = 0x0060704c;
    std::uintptr_t factory_table_slot = 0x00606fd0, factory_count_slot = 0x00607050;
    std::uintptr_t asteroid_table_slot = 0x00606ffc, asteroid_count_slot = 0x0060707c;
    // The big tiers' clone row R' (explosion-look.md section 1): the Effects row table (spawn note 10.1) and the EXE's
    // malloc (0x005112c4, cdecl: the allocator 0x00414ba0 uses, so the engine's reset 0x00412c80 frees the element arrays
    // with its matching free 0x0050e1b0). Present builds R' through them; the handler reads the table only.
    std::uintptr_t effects_rows = 0x00580068;
    std::uintptr_t malloc_va = 0x005112c4;
};
static_assert(Globals{}.dock_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_dock &&
                  Globals{}.factory_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_factory &&
                  Globals{}.asteroid_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_asteroid &&
                  Globals{}.ship_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_ship &&
                  Globals{}.missile_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_missile &&
                  Globals{}.asteroid_count_slot == Globals{}.bullet_count_slot + 4 * layout::class_asteroid &&
                  Globals{}.dock_count_slot == Globals{}.bullet_count_slot + 4 * layout::class_dock,
              "the type tables are indexed by class (0x00606fb8 + 4 class, counts 0x00607038 + 4 class)");

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
    // Step 4 (docs/architecture/bolt-muzzle-look.md section 1): the weapon's mount tier (0 fighter, 1 corvette, 2 capital:
    // the smallest ship class that mounts it; a row the generator did not resolve keeps 0, the fighter fallback,
    // unknown 7) and its range in metres (speed x lifetime; 0: unknown, the stage takes the live record's).
    std::uint8_t mount_tier[table_capacity]{};
    std::uint16_t range_m[table_capacity]{};
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
    static constexpr unsigned capacity = 96; // drawn: >= max_owners + hull::max_owners + boom::max_bodies (asserted)
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

// ---------------------------------------------------------------------------------------------- the death explosion (3.2)
// Step 3 (docs/architecture/explosion-look.md, the ratified spec; weapon-look.md 3.2 where it is silent): the death
// sites' tiers, the one decision "ours / native" per record (decide), and the look's per-tier table and laws. The
// integer half (Tier, tier_native, decide, record_life_ms) is the death handler's; the floats are the stage's and the
// lab reads them by name (`name = value`), as for `look` and `hull`. A flight observation flips a constant here, never
// a setting (note section 10).
namespace boom {
// The death tiers: fighter, corvette, capital (patch_tier_of_ship, the radius bands for an unknown id), station
// (classes 5 / 6), missile (class 10); asteroid (class 17, always native: rock geometry, weapon-look 3.2) and other
// (any other class: native). The first five have a law below.
enum Tier : std::uint8_t { fighter = 0, corvette = 1, capital = 2, station = 3, missile = 4, asteroid = 5, other = 6,
                           tier_count = 7 };
constexpr unsigned drawn_tiers = 5;
inline const char* tier_name(unsigned t) noexcept {
    static const char* const names[tier_count] = {"fighter", "corvette", "capital", "station", "missile", "asteroid", "other"};
    return t < tier_count ? names[t] : "?";
}
// The tiers forwarded native (a bit per Tier; section 10's last row: a tier that still reads wrong in flight joins it).
// None since the big tiers' clone row (2026-11-07): corvette, capital and station forward through R' (tier_swap).
constexpr std::uint8_t tier_native = 0; // a literal the lab reads
// The tiers whose native spawn runs with the type record's death column pointing at the clone row R' for that one call
// (section 1: the engine's carrier keeps the hull copy through the pre-roll and fades it over P .. P + 1 s; its sprites
// DISTANT, its PROPAGATE children dropped): corvette, capital and station. The others (fighter, missile) skip the spawn.
constexpr std::uint8_t tier_swap = 14; // (1 << corvette) | (1 << capital) | (1 << station); a literal the lab reads
static_assert(tier_swap == ((1u << corvette) | (1u << capital) | (1u << station)), "the big tiers take R'");
// The seam: whether a record of this tier is the stage's with its native spawn skipped (ours), the stage's with the
// native spawn forwarded through R' (swap), or forwarded native. The one place the decision is taken.
enum Decision : std::uint8_t { decide_native = 0, decide_ours = 1, decide_swap = 2 };
constexpr Decision decide(unsigned tier) noexcept {
    return tier >= drawn_tiers || (tier_native & (1u << tier)) ? decide_native
           : (tier_swap & (1u << tier))                         ? decide_swap
                                                                : decide_ours;
}
inline const char* decision_name(unsigned d) noexcept {
    return d == decide_ours ? "ours" : d == decide_swap ? "swap" : "native";
}
// Why a death record is what it is (a death record's Event::tier; the weapon_death row's `cause=`): ours (the spawn
// skipped), swapped through R', or forwarded native because the tier is native, the frame was not the stage's (latch,
// programs, the family), R' was not ready for the type record's row (not built, a zeroed or foreign slot: the tier stays
// native for that death, `reason=tier_native`), the type record was unreadable, or the swap stack was full.
enum Cause : std::uint8_t { cause_ours = 0, cause_swap = 1, cause_tier_native = 2, cause_not_owned = 3,
                            cause_clone_not_ready = 4, cause_no_record = 5, cause_swap_full = 6, cause_count = 7 };
inline const char* cause_name(unsigned c) noexcept {
    static const char* const names[cause_count] = {"ours", "swap", "tier_native", "not_owned", "clone_not_ready",
                                                   "no_record", "swap_full"};
    return c < cause_count ? names[c] : "?";
}
enum Mode : std::uint8_t { full = 0, simple = 1 };
constexpr Mode mode = full; // simple (section 10): the same body, shock off, embers halved, smoke off
// The per-tier table (section 3; index Tier, the five drawn tiers): R_f = f R_obj clamped to [min, max] m; the life L
// and the pre-roll (game ms); the rise fraction rho; R_0 = r0_obj R_obj + r0_f R_f; embers per death; the shock (1) and
// its tau as a share of L.
constexpr float tier_f[5] = {2.0f, 1.5f, 0.9f, 0.6f, 3.5f};
constexpr float tier_min_m[5] = {12.f, 30.f, 80.f, 200.f, 6.f};
constexpr float tier_max_m[5] = {50.f, 160.f, 600.f, 1800.f, 25.f};
constexpr float tier_life_ms[5] = {2000.f, 3000.f, 4000.f, 6000.f, 800.f};
constexpr float tier_preroll_ms[5] = {0.f, 0.f, 1500.f, 3000.f, 0.f};
constexpr float tier_rise[5] = {0.12f, 0.15f, 0.15f, 0.20f, 0.10f};
constexpr float tier_r0_obj[5] = {0.5f, 0.5f, 0.f, 0.f, 0.5f};
constexpr float tier_r0_f[5] = {0.f, 0.f, 0.5f, 0.5f, 0.f};
constexpr float tier_embers[5] = {32.f, 64.f, 128.f, 160.f, 12.f};
// Section 14 (2026-11-07, the vanilla frames: a dense spark spray): the sparks a death, hashed per death in
// [tier_sparks_lo, tier_sparks_hi] (the secondaries' share secondary_embers each part of it); tier_embers stays the span of
// the main set's indices the fragments come from (one in fragment_every of the first tier_embers), so the fragments keep
// their count. A frame draws at most spark_frame_cap sparks and what the sprite room leaves: past either every second
// spark of every death is shed, then every fourth (the half-count shed, spark_shed_max halvings at most; the long
// streaks keep their share, the fragments (indices 7 mod fragment_every) their full count through three halvings and
// half of it at the fourth).
constexpr float tier_sparks_lo[5] = {120.f, 160.f, 240.f, 240.f, 40.f};
constexpr float tier_sparks_hi[5] = {240.f, 300.f, 400.f, 400.f, 80.f};
constexpr unsigned spark_frame_cap = 1024, spark_shed_max = 4;
// The sparks' quads are charged to the family's budget (budget_weight_flash a screen, as the rays and stars); the frame's
// spark area, estimated before the build per death from its mean quad (a spark_est_head px head, a spark_est_len px
// length, both at 1080 rows, the length x min(1, R_f on screen / spark_ref_px)), takes at most spark_budget_share of the
// budget: past it the half-count shed halves the sparks again (within spark_shed_max).
constexpr float spark_budget_share = 0.25f, spark_est_head = 4.f, spark_est_len = 16.f;
constexpr float tier_shock[5] = {0.f, 1.f, 1.f, 1.f, 1.f};
constexpr float tier_shock_tau[5] = {0.3f, 0.3f, 0.3f, 0.4f, 0.3f};
// 2.1 the body: the burst program unchanged (look::fireball_*), R(t) = R_0 + (R_f - R_0) (1 - e^(-t / (rho L))) +
// body_grow R_f t / L; T(t) = 1 - body_cool t / (body_hold L) to body_hold L, then linear to 0 at L; E_b = body_e
// (look::body_max); biased body_bias R(t) towards the eye; occlusion class 2 (SOFT 1.0: the program's lane cut).
// body_r_sphere: the drawn sphere's radius over R(t): the burst's puff_box rule (1 + lobe_amp = 1.25) put the body's mean
// outline (36 bins, 10 % of its peak) at 0.97 / 0.91 R(t) at 0.1 / 0.3 L (the column's and the rim erosion's loss,
// measured natively 2026-11-07); 1.33 brings both within 5 % (death_tier_sizes).
constexpr float body_grow = 0.1f, body_hold = 0.8f, body_cool = 0.7f, body_e = 1.5f, body_bias = 0.5f;
constexpr float body_r_sphere = 1.33f;
// The drawn sphere's factor by the body's projected radius (px at 1440 rows; 2026-11-07, the big tiers): the 10 %-of-peak
// outline grows with the pixel size at body_r_sphere (0.98-1.01 R(t) at 57-82 px, the corvette; 1.07 at 280-380 px, the
// station; 1.10-1.14 at 340-457 px, the capital [M]), so the factor falls linearly from body_r_sphere at sphere_px0 to
// body_r_sphere_big at sphere_px1 and past; an eye inside the sphere keeps body_r_sphere (secondaries likewise).
constexpr float body_r_sphere_big = 1.21f, sphere_px0 = 64.f, sphere_px1 = 320.f;
// Section 3's floor: a body never under body_floor_px of radius at 1440 rows (scaled by the target's rows); a floor
// raises size, never radiance.
constexpr float body_floor_px = 12.f;
// The white-hot core (coordinator decision 2026-11-07, after the first frames: the body itself carries the birth's white
// heat, not the embers): the fireball program's radiance x (1 + g) and its colour whitened by min(1, g) inside core_r of
// the body's radius (falling to 0 at core_r + core_soft), g = core_gain (1 - x / core_x)^2 while x < core_x (x = t / L).
// Section 14 (2026-11-07, the vanilla frames: a blazing core): core_gain 2.5 -> 5 and the decay squared (linear, the
// capital's core stayed a white blob at 0.3 L in the frames), so at 0.05 L the core is E_b (1 + 3.67) = 7.0 x the
// column (>= 6 inside 0.2 R, the HDR path blooms it), at 0.1 L E_b (1 + 2.55), at 0.2 L E_b (1 + 0.92), gone by core_x
// 0.35 L (it was 2.5 (1 - x / 0.35): E_b (1 + 1.79) at 0.1 L).
constexpr float core_gain = 5.0f, core_x = 0.35f, core_r = 0.3f, core_soft = 0.2f;
// A big tier's body is born over its burning secondaries (the pre-roll): its core gain eases in over core_ramp_ms game ms
// (three frames at 16 ms) or the record's latched three-frame floor when longer (SETA), so the main blast never doubles
// the frame's peak at once (death_big_no_pop_at_birth: 2.09 x without).
constexpr float core_ramp_ms = 48.f;
// Section 14, the halo spike at birth: the sprite batch's flash kind (a white disc core and its e-folded halo) at the
// body's centre biased to its front, its core spike_rf of R_f on screen clamped to spike_px_lo..hi px at 1080 rows (scaled
// with the rows), its halo spike_halo core radii, radiance spike_e (1 - x / spike_x)^2, gone at spike_x L (the three-frame
// floor), the halo heat(spike_t).
constexpr float spike_rf = 0.15f, spike_px_lo = 4.f, spike_px_hi = 8.f, spike_halo = 4.f, spike_e = 6.f, spike_x = 0.12f,
                spike_t = 0.95f;
// Section 14, the rays (2026-11-07 soft rays, after the user's "not so pronounced/thin ... look into our sun": the sun's
// spokes, own-sun-look.md step 1d): an odd ray_n_lo..hi rays a death (hashed: no two opposite), each a sprite of the ray
// kind (weapon_sprite_ps.hlsl, depth.y 5) from ray_start R(t) out (the body's rim: 2026-11-08, from 0.3 R(t) the body and
// the rim halo swamped every short ray, the own sun's "why the rays vanished") at a stratified hashed angle
// (slot i of N at (i + ray_jitter_lo + ray_jitter h) / N turns plus a hashed turn for the death: the slots' offsets within
// 0.375 .. 0.625, so two rays sit at least a quarter slot off opposite), its length skewed (2026-11-08, the user's "3 big
// rays which have almost the same length"): L(g) = (ray_len_lo + ray_len_k g^2) R_f on screen beyond the rim, g_i = frac(ray_phi (i +
// o)) (the golden-ratio hash, o hashed per death), the three largest g of the death (their rank fixed per death) at L(g_1),
// ray_top2 L(g_1) and ray_top3 L(g_1), every other ray at L(g) ray_rest ray_top3 L(g_1) / L(g_4) (g_4 the fourth largest:
// it at ray_rest of the third), so the three longest differ by >= 25 % and most are short; x (ray_grow0 + (1 -
// ray_grow0) min(1, x / ray_grow_x)); the along-profile in u = along / length, so each ray's visible tip follows its
// length; across a Gaussian of FWHM max(ray_fwhm_px
// px at 1080 rows (scaled with the rows), ray_fwhm_len x its length x (1 + u / 2)), u = 0 at the start .. 1 at the tip (the
// spoke widening outwards); along e^(-u) (1 - u)^2 (the sun's, at full strength from the rim), faded in over the first
// ray_in of it (smoothstep: no
// bright base); its brightness (ray_s_lo + ray_s_span g2_i) of ray_e (g2_i = frac(ray_phi2 (i + o) + 0.31)) x (1 - x /
// ray_x)^ray_pow x the flicker: per ray a hashed period ray_flicker_lo..hi game ms (at least two frame steps: under SETA
// a level holds at least two frames), its level the hash of the period's index blended smoothly into the next one's, 1 -
// ray_flicker x level; colour heat(T) on T ray_t0 at birth to ray_t1 at ray_x L; at the body's front (z - R(t)), SOFT
// ray_soft R_f against the lane; gone at ray_x L. The quad a trapezoid of half width ray_quad_w x the FWHM at either end
// (the Gaussian's tail there under 0.5 % of its peak). A frame draws at most ray_frame_cap ray quads (past it every second
// ray of every death, then every fourth: the half-count shed, ray_shed_max halvings at most).
constexpr float ray_n_lo = 15.f, ray_n_hi = 25.f, ray_jitter_lo = 0.375f, ray_jitter = 0.25f;
constexpr float ray_len_lo = 0.4f, ray_len_k = 2.0f, ray_top2 = 0.78f, ray_top3 = 0.6f, ray_rest = 0.8f;
constexpr float ray_phi = 0.6180340f, ray_phi2 = 0.7548777f, ray_grow0 = 0.7f, ray_grow_x = 0.06f, ray_start = 1.0f;
static_assert(ray_top2 * 1.25f <= 1.f + 1e-6f && ray_top3 * 1.25f <= ray_top2 + 1e-6f && ray_rest * 1.25f <= 1.f + 1e-6f,
              "the three longest rays differ by at least 25 %, the fourth 25 % under the third");
constexpr float ray_fwhm_px = 6.f, ray_fwhm_len = 0.065f, ray_in = 0.05f, ray_quad_w = 1.4f;
// ray_e 1.0 (2026-11-08, was 0.4): a ray's base about 0.8-1.0 of the rim halo under it (halo_e 0.5 E_b T at the rim).
constexpr float ray_e = 1.0f, ray_s_lo = 0.6f, ray_s_span = 0.6f, ray_x = 0.5f, ray_pow = 3.f;
constexpr float ray_flicker_lo = 150.f, ray_flicker_hi = 300.f, ray_flicker = 0.3f;
constexpr float ray_t0 = 0.95f, ray_t1 = 0.8f, ray_soft = 1.0f;
constexpr unsigned ray_frame_cap = 512, ray_shed_max = 3;
// The ray hashes: element_hash(sequence, ray_element + i) per ray (angle, length, width, brightness, period), the count
// and the death's turn from ray_element - 1; past the secondaries' (secondary_element + 4 x 12).
constexpr unsigned ray_element = 1024;
static_assert(unsigned(ray_n_hi) <= 64u, "the rays' hashes stay below ember_element");
// 2.2 the flash: the burst's star at A = star_arm R_f, core star_core R_f, white at look::core_e, the arms heat(star_t),
// star_ms game ms (three-frame floor), its arm on screen capped at star_cap_h of the target's rows.
constexpr float star_arm = 0.4f, star_core = 0.08f, star_ms = 40.f, star_t = 0.9f, star_cap_h = 0.25f;
// 2.3 the shock: r(t) = shock_r_x R_f (t / tau)^0.5, shell width shock_w r; the limb only (decision 2026-11-07: the
// filled shell read as a grey disc): the chord's density x a ramp from shock_limb r inside the limb (interior 0); colour
// heat(T(t)) of the body's age x shock_e x (1 - t / (shock_end L))^2, gone at shock_end L; not drawn once the eye is
// inside it. missile_shock: the kill switch of the missile's shell (decision 6: off if the limb-only shell still reads as
// a disc in the frames).
constexpr float shock_r_x = 4.0f, shock_w = 0.06f, shock_e = 1.0f, shock_limb = 0.15f, shock_end = 0.25f;
// Off for the first flight (decision 6 applied 2026-11-07: the limb-only shell read as a flat ring in the frames).
constexpr bool missile_shock = false;
// The tiers that draw the shell (a bit per Tier; coordinator decision D14, 2026-11-07, from the big tiers' frames: the
// corvette's expanding grey ring and the white ring at the capital's and station's birth are the soap-bubble failure of
// section 10): none. The program stays built and optional; the fixture forces the shell for its law gates only
// (BoomTuning::force_shock). The planned replacement is a refraction limb through the engine shimmer pass (the hull
// shock front's), not a thinner ring.
constexpr std::uint8_t shock_tiers = 0;
// 2.4 embers: ember_cones_lo..hi seed cones (axis uniform, half-angle ember_cone_deg, both hashed per event), speed
// v_e = clamp(ember_v_x R_f per s, ember_v_min, ember_v_max) m/s x a hashed factor in [ember_speed_lo, ember_speed_hi]
// at constant speed (the linear branch), from the centre +- ember_p0 R_f within the unit ball, never past ember_hide_r
// R_0 (launched inside the newborn body: none drawn at birth, they emerge); lives ember_life[3] L by thirds x (1 +-
// ember_stagger / 2); E ember_e e^(-t / (ember_tau L)) T, colour heat(T), T = 1 - t / life (a cooling
// spark dims as it reddens: the frame's mean hue follows the hot ones). Decision 2026-11-07 (the first frames: a tangle
// of white sticks over the body, then uniform rods): an ember is hidden while inside the body (fully inside
// ember_hide_r R(t), visible past R(t), the hiding fading with the body's erosion from ember_hide_x0 to ember_hide_x1 of
// L), its width ember_w_px_lo..hi px at 1080 rows (hashed, scaled with the rows; the program's pixel mode: no floors),
// its length a hashed ember_len_lo..1 of ember_len_max R_f (never under two widths), a bright head and a fading tail.
// One in fragment_every a fragment: fragment_v v_e, fragment_life L, width fragment_w_px px, half an ember's length, a
// hot tip at heat(fragment_tip_t) cooling and a dark box under it (the scorch pass, darkening fragment_dark,
// surface-bound), hidden inside the body likewise.
// Section 14 (2026-11-07, the vanilla's spray from t = 0; supersedes the D5 hiding and the R_f-scaled lengths): a spark
// of a body's set leaves from spark_r0_lo..spark_r0_lo + spark_r0_span of R_0 out along its own direction (visible at
// birth); spark_cone_share of them in the seed cones, the rest in a uniform direction; speeds ember_speed_lo..hi v_e;
// hidden only within ember_hide_r R(t) (fading in over the inner spark_hide_soft of that radius), the hiding fading with
// the body's erosion from ember_hide_x0 to ember_hide_x1 of L; its head a Gaussian of FWHM ember_w_px_lo..hi px at 1080
// rows (hashed; 2026-11-07 "too thin": x min(1, R_f on screen / spark_ref_px), never under ember_w_px_floor) in a halo of
// spark_halo_e of its radiance to spark_halo_r head radii (2^(-(d / (spark_halo_r r))^2), windowed to 2 spark_halo_r r),
// its tail a Gaussian of FWHM 0.5 + 0.5 x the head's at the head (2-3 px) to 1 px at the tail's end, (s)^2 along; its
// length a hashed spark_len_lo..hi px at 1080 rows (h^2: more short ones), one in spark_long_every a long streak of
// spark_long_lo..hi px with a spark_long_w px head (the floor: its tail 2 px at the head), its head x spark_long_e;
// lengths x min(1, R_f on screen / spark_ref_px at 1080 rows), at most spark_cap_r R(t) on screen while the body draws,
// never under two head widths; a fragment's head fragment_w_px, its profile the spark's; head radiance ember_e e^(-t / (ember_tau L)) T x vis on heat(spark_t0 T) (ember_e
// 2.6: a head decodes to 8.2 linear, the tonemap's white at the Auto cap [I, the meter's arithmetic];
// death_exposure_meter_shift measures the production meter's shift on a fighter at 300 m and 120 m, not that limit).
constexpr float ember_cones_lo = 4.f, ember_cones_hi = 6.f, ember_cone_deg = 25.f;
constexpr float ember_v_x = 1.5f, ember_v_min = 40.f, ember_v_max = 300.f, ember_p0 = 0.3f;
constexpr float ember_speed_lo = 0.5f, ember_speed_hi = 2.0f;
constexpr float ember_life[3] = {0.4f, 0.8f, 1.2f};
constexpr float ember_stagger = 0.4f, ember_e = 2.6f, ember_tau = 0.8f;
constexpr float ember_w_px_lo = 3.f, ember_w_px_hi = 5.f, ember_w_px_floor = 3.f, spark_halo_r = 2.f, spark_halo_e = 0.12f;
constexpr float ember_len_max = 0.35f, ember_len_lo = 0.25f;
constexpr float ember_hide_r = 0.5f, ember_hide_x0 = 0.5f, ember_hide_x1 = 0.9f;
constexpr float spark_r0_lo = 0.55f, spark_r0_span = 0.45f, spark_cone_share = 0.6f, spark_hide_soft = 0.2f;
constexpr float spark_len_lo = 6.f, spark_len_hi = 36.f, spark_long_every = 20.f, spark_long_lo = 30.f, spark_long_hi = 60.f;
// A spark's drawn length at most spark_cap_r R(t) on screen (2026-11-07: the long streaks crossed the whole body as hard
// rays in the frames); its radiance x spark_gain (the head's peak ember_e spark_gain: the meter, death_exposure_meter_shift).
constexpr float spark_cap_r = 1.2f, spark_gain = 0.8f;
constexpr float spark_long_w = 3.f, spark_long_e = 1.0f, spark_ref_px = 28.8f, spark_t0 = 1.0f;
// A spark leaves at a hashed delay in [0, spark_birth_x L) of its set, spark_birth_now of them at once (a spray from
// t = 0, not one stacked burst: 200 heads on one ring at birth summed to E 50+ in the first frames); its clock, life and
// cooling start then, its radiance from spark_in0 of it to the whole over its first spark_in_x L (a fresh spark sits in
// the crowd at the launch ring).
constexpr float spark_birth_x = 0.05f, spark_birth_now = 0.15f, spark_in_x = 0.02f, spark_in0 = 0.25f;
// The spray's speeds lean fast (ember_speed_lo + (hi - lo) sqrt(h): a spray beyond the core, not a ball inside it) and a
// spark stays hot most of its life: T = spark_t0 (1 - (t / life)^spark_cool_pow) (the vanilla's yellow sparks late; a
// linear T read orange from 0.4 L in the frames).
constexpr float spark_cool_pow = 1.5f;
constexpr float fragment_every = 8.f, fragment_v = 0.5f, fragment_life = 1.5f, fragment_w_px = 3.f;
constexpr float fragment_dark = 0.6f, fragment_tip_t = 0.6f, fragment_dark_r = 1.f;
// 2.6 the light (decision 2026-11-07: the note's 5 R_f read as a lamp over a hull; the burst's accepted law instead,
// hull-burst-look.md section 3): the glow primitive on the lane's surfaces at R = min(glow_r_x R(t), glow_r_max R_f),
// E_g = glow_e E_b T(t) heat(T(t)), while the body draws; and the same light in air about the body (the halo,
// weapon_halo_ps.hlsl: occlusion class 1, its soft cut halo_soft x its quad's radius), so a body has its own bloom; not
// drawn with the eye inside the body. The cluster rule over the event frame's bodies (N by remaining life within
// cluster_r_x R_i, cluster_n0).
// The halo (2026-11-08, the user: "a halo which falls off from the heat source: on the edges it should be bright then
// fall off"; the own sun's corona, own-sun-look.md step 1d), every tier: a rim glow about the body's 10 %-of-peak
// outline R = R(t), radiance halo_e E_b T(t) heat(halo_t T(t)) (the cluster's saturation; the body's rim heat, its
// orange fringe: heat(T) read as a grey disc under AgX in the lab) x p(r), r the pixel's ray's distance
// from the centre (the impact parameter), d = (r - R) / R: p = (e^(-(d / halo_sigma)^2) + halo_tail w(d) / (1 + d^2)) /
// (1 + halo_tail) outside R (peak 1 at the rim), w = 1 - smoothstep(halo_win_x0 - 1, halo_r_x - 1, d) (0 at halo_r_x
// R), inside R the rim's value x smoothstep(halo_in, 1, r / R) (no fill: the body is not a balloon, the rise hidden
// under the body's rim); the quad to halo_r_x R(t); faded out over halo_fade_x0 .. halo_fade_x1 of L (smoothstep, with
// the core). It replaces the 2026-11-07 filled glow in air (1.6 R(t), D15's 1.15 R(t) and 0.2 E_b cap on the big tiers:
// it read as a balloon). At d = 1 (2 R(t)) p = 0.050, its 10 % radius 1.80 R(t) [I, the law] (the first lab round's
// halo_tail 0.4 windowed from 1.8 R(t), 0.114 and 2.04 R(t), read as a disc with an edge).
constexpr float glow_r_x = 1.6f, glow_r_max = 2.0f, glow_e = 0.33f, halo_soft = 0.5f, halo_fade_x0 = 0.3f, halo_fade_x1 = 0.6f;
constexpr float halo_e = 0.5f, halo_r_x = 2.5f, halo_sigma = 0.35f, halo_tail = 0.25f, halo_win_x0 = 1.5f, halo_in = 0.9f,
                halo_t = 0.8f;
constexpr float cluster_r_x = 2.0f, cluster_n0 = 1.6f;
// 2.7 the smoke: the burst's surface-bound multiply puff at smoke_r R_f, y = (t - smoke_t0 L) / (smoke_span L),
// strength smoke_s0 min(1, y / smoke_rise) (1 - y) / (1 - smoke_rise), the body's seed and rotation.
constexpr float smoke_r = 1.1f, smoke_t0 = 0.5f, smoke_span = 1.1f, smoke_s0 = 0.45f, smoke_rise = 0.27f;
// 2.5 and 4, the pre-roll of the capital and the station (tier_preroll_ms P): tier_secondaries secondaries at a
// geometric cadence (ratio tier_cadence: the first at 0, the gaps g0 r^k summing to P, g0 = P (1 - r) / (1 - r^(N - 1)),
// each gap x (1 +- secondary_jitter) by the seed, no birth past P): the capital's 0.49 0.34 0.24 0.17 0.12 0.08 0.06 s,
// the station's 0.66 ... 0.07 s. Each is the burst body at R_s = secondary_r R_f (the last two secondary_r_last R_f, the
// size the body is born at; the station's two before them secondary_r_big R_f), life secondary_life L, rise
// secondary_rise, E body_e with the body's white-hot core, its own star (A = star_arm R_s), its glow on surfaces
// (glow_r_x R(t), at most glow_r_max R_s, glow_e) and secondary_embers embers (8 per secondary: half the tier's; the rest
// at the body's birth), at a hashed point inside the hull box: the owner's box from the bubble cache (unknown 4: the
// weapon_death row's bubble_cached=), else the axis capsule secondary_capsule_len R_obj long and secondary_capsule_r R_obj
// in radius along the node's row 2; secondary_inset of the way to the box's faces. The glows of an event's live
// secondaries saturate by s(N) = 1 / (1 + (N - 1) / secondary_cluster_n0), N by remaining life. Counts: max_secondaries.
constexpr unsigned tier_secondaries[5] = {0, 0, 8, 12, 0};
constexpr float tier_cadence[5] = {0.f, 0.f, 0.7f, 0.8f, 0.f};
constexpr float secondary_jitter = 0.2f, secondary_r = 0.35f, secondary_r_last = 0.5f, secondary_r_big = 0.6f;
constexpr float secondary_life = 0.3f, secondary_rise = 0.1f, secondary_inset = 0.85f;
constexpr float secondary_capsule_len = 1.4f, secondary_capsule_r = 0.25f, secondary_cluster_n0 = 2.5f;
constexpr unsigned secondary_embers = 8, max_secondaries = 64, max_glows = 128;
static_assert(tier_secondaries[2] * secondary_embers <= 128u && tier_secondaries[3] * secondary_embers <= 160u,
              "the secondaries' embers are part of the tier's count");
// A secondary's radius over R_f (k of the tier's N).
constexpr float secondary_scale(unsigned tier, unsigned k) noexcept {
    return k + 2u >= tier_secondaries[tier < 5 ? tier : 0] ? secondary_r_last
           : tier == 3 && k + 4u >= tier_secondaries[3]     ? secondary_r_big
                                                              : secondary_r;
}
// 2.6 the dying hull's light through the pre-roll: the glow primitive at the hull box's centre, R = hull_light_r R_obj,
// E = glow_e E_b (hull_light_e0 + (hull_light_e1 - hull_light_e0) t / P) heat(hull_light_t) from t = 0 to P, then falling
// linearly to 0 with the hull copy's fade over P .. P + clone_fade_ms (2026-11-07: ended at P, the hull dropped from lit
// to grey in one frame in the dumps).
constexpr float hull_light_r = 1.f, hull_light_e0 = 0.4f, hull_light_e1 = 0.8f, hull_light_t = 0.6f;
// A secondary k's hashes: element_hash(sequence, secondary_element + 4 k + j) (j 0 the gap, 1 the point, 2 the rotation
// and lobes, 3 the noise seed), past every ember's.
constexpr unsigned secondary_element = 512;
// Section 5's coverage dimming (off: death_exposure_meter_shift decides): E_b / (1 + 0.5 max(0, cov - 0.5) / 0.5).
constexpr bool coverage_dim = false;
// Section 7: the shock's budget weight (the limb-only shell measured 2026-11-07 at 5120x1440: 0.356 a screen, the fence
// removed, shock_weight row; the 0.76 first quoted came from the filled shell's 0.113-screen box, the fence's error
// dominating), the halo's (the glow's), the count caps.
constexpr float budget_weight_shock = 0.36f, budget_weight_halo = 0.40f;
// The family's own cap (screens charged before the shed order runs, below the shared screen_budget's remainder): the
// close capital's 450 us fence at 5120x1440 (the note's 3-screen ceiling 532 us less its margin; 2026-11-07: without it
// six live secondaries near the eye charged 6.6 screens, 907 us [M]).
constexpr float budget_screens = 2.5f;
constexpr unsigned max_bodies = 32, max_quads = 2048;
// The record's element hashes (stage::element_hash(sequence, k)): the body's rotation, lobes and noise seed are the
// burst's elements 8 / 10 / 4, the star's 11; the cones from cone_element, an ember's from ember_element + its index
// (2048 since section 14: up to 400 + 12 x 8 sparks a death stay clear of the secondaries' and the rays' hashes).
constexpr unsigned cone_element = 32, ember_element = 2048;
static_assert(ember_element > ray_element + 64u && ray_element > secondary_element + 4u * 12u, "the hash ranges apart");
// The record's life (game ms): the pre-roll plus the slowest element's (the smoke's (smoke_t0 + smoke_span) L).
constexpr std::uint16_t record_life(unsigned t) noexcept {
    return std::uint16_t(tier_preroll_ms[t] + (smoke_t0 + smoke_span) * tier_life_ms[t]) + 1u;
}
constexpr std::uint16_t record_life_ms[drawn_tiers] = {record_life(0), record_life(1), record_life(2), record_life(3),
                                                       record_life(4)};
static_assert(record_life_ms[station] > record_life_ms[capital] && record_life_ms[missile] > 1000u,
              "the record lives to the smoke's end (u16 game ms)");
// Section 1, the clone row R' (spawn note 10.5 (b); built at Present, weapon_effects_patch.cpp): at most clone_rows
// clones (one per source row and pre-roll), a source row of at most clone_max_elements elements; free slots (count and
// array 0) searched from clone_slot_hi down to clone_slot_lo (installed 901-999 and stock 203-999 free [M census]). The
// handler's swap stack holds swap_depth_max nested big-tier deaths (an area blast inside one forwarded call).
constexpr unsigned clone_rows = 16, clone_max_elements = 32, clone_slot_lo = 203, clone_slot_hi = 999, swap_depth_max = 8;
constexpr std::uint32_t clone_fade_ms = 1000; // FADEOUTBODY's fixed ramp (spawn note 10.2): the carrier held to P + 1 s
// The tier's pre-roll in whole game ms (R''s FADEOUTBODY start): an integer table (the handler holds no float).
constexpr std::uint32_t tier_preroll_whole_ms[drawn_tiers] = {
    std::uint32_t(tier_preroll_ms[0]), std::uint32_t(tier_preroll_ms[1]), std::uint32_t(tier_preroll_ms[2]),
    std::uint32_t(tier_preroll_ms[3]), std::uint32_t(tier_preroll_ms[4])};
constexpr std::uint32_t preroll_ms(unsigned t) noexcept {
    return t < drawn_tiers ? tier_preroll_whole_ms[t] : 0u;
}
// R' from the source row's elements (count n, 0x70 bytes each) into out (room for n + 2): every element copied with
// DISTANT set and FADEOUTBODY / WRECK cleared, its start clamped to P (a masked element pending past P + 1 s would hold the
// carrier); the PROPAGATE element dropped; then one empty FADEOUTBODY node (body 0, TIMEOUT 1000 ms) starting at P and
// one empty TIMEOUT node from 0 to P + 1000 ms (it holds the instance and so the carrier to the fade's end). Returns the
// clone's count; *masked the copies, *dropped the PROPAGATE elements. Integer only, plain byte stores.
struct CloneCounts {
    unsigned masked = 0, dropped = 0, kept = 0;
};
inline unsigned clone_elements(const unsigned char* src, unsigned n, std::uint32_t P, unsigned char* out,
                               CloneCounts* c) noexcept {
    namespace L = layout;
    auto get = [](const unsigned char* b, unsigned off) {
        return std::uint32_t(b[off]) | std::uint32_t(b[off + 1]) << 8 | std::uint32_t(b[off + 2]) << 16 |
               std::uint32_t(b[off + 3]) << 24;
    };
    auto put = [](unsigned char* b, unsigned off, std::uint32_t v) {
        for (unsigned k = 0; k < 4; ++k) b[off + k] = std::uint8_t(v >> (8 * k));
    };
    unsigned k = 0;
    *c = CloneCounts{};
    for (unsigned i = 0; i < n; ++i) {
        const unsigned char* e = src + i * L::element_stride;
        const std::uint32_t f = get(e, L::el_flags);
        if (f & L::eedf_propagate) {
            ++c->dropped;
            continue;
        }
        unsigned char* o = out + k * L::element_stride;
        for (unsigned b = 0; b < L::element_stride; ++b) o[b] = e[b];
        put(o, L::el_flags, (f | L::eedf_distant) & ~(L::eedf_fadeoutbody | L::eedf_wreck));
        if (get(o, L::el_start) > P) put(o, L::el_start, P);
        ++c->masked;
        ++k;
    }
    for (unsigned j = 0; j < 2; ++j) {
        unsigned char* o = out + k * L::element_stride;
        for (unsigned b = 0; b < L::element_stride; ++b) o[b] = 0;
        put(o, L::el_flags, j ? L::eedf_timeout : (L::eedf_fadeoutbody | L::eedf_timeout));
        put(o, L::el_start, j ? 0u : P);
        put(o, L::el_timeout, j ? P + clone_fade_ms : clone_fade_ms);
        ++c->kept;
        ++k;
    }
    return k;
}
// Section 13.1, the fire wave (capital and station deaths; 2026-11-07): a thick planar fire front born on the body's
// frame in the plane of the node's rows 2 (long) and 1 (up), normal row 0 (the view plane when the rows are unread:
// weapon_death wave_plane=). In-plane distance d' with the long axis stretched by wave_stretch; the front's radius
// r(t) = r_0 + (r_max - r_0)(1 - (1 - t / tau)^3) for t <= tau, then + wave_creep (r_max - r_0) per tau (never stops),
// r_0 the hull shape's half length along row 2, r_max wave_r_x R_f, tau wave_tau_x L, gone at L_w = wave_life_x L by
// (1 - t / L_w)^2. In z = (d' - r) / w_e, w_e wave_w_x R_f, the material of v7 below (the fireball's on the angle and the
// distance behind the front); across the plane a slab wave_slab_x R_f thick, (1 - (2 y / h)^2), its chord normalised as
// the body's column (edge-on a bar, never a line); E_w wave_e. The circumference broken as the hull ring's: m = wave_m_floor + (1 - wave_m_floor) saturate(0.5 + wave_m_contrast (n - 0.5)),
// n = wave_n_w[0] vn1(wave_n_cells[0] phi / 2 pi + s) + wave_n_w[1] vn1(wave_n_cells[1] phi / 2 pi + s'), periodic.
// The early-out window -wave_window_in < z < wave_window_out; the swept hulls: a lane point inside the slab's front
// window adds the rim and the band's floor at wave_light E_w. Refraction ahead (the engine shimmer pass's plane mode): the
// band [r, r + wave_refract_band w_e] displaced outward by wave_refract_px at 1440 rows x (1 - t / L_w)^2 through the
// band's eight octagon strips (the newest wave only; past engine_shimmer::weapon_ring_max_screens drawn without a band).
// Budget (section 7): the annulus's clipped screens x budget_weight_wave + the rest of the box x budget_weight_wave_miss;
// shed at step 3 (after the smoke, before the glows and secondaries); the strips charged x hull::budget_weight_shimmer
// and shed first. max_waves boxes a frame.
constexpr std::uint8_t wave_tiers = 12; // (1 << capital) | (1 << station); a literal the lab reads
static_assert(wave_tiers == ((1u << capital) | (1u << station)), "the fire wave: capitals and stations");
constexpr float wave_r_x = 7.f, wave_tau_x = 0.25f, wave_life_x = 0.4f, wave_creep = 0.05f, wave_stretch = 1.25f;
// Built (2026-11-07, the first native frames): wave_m_floor 0.1 -> 0.05 (every one of 64 bins lit at 0.1: no broken arc).
// The kill step (2026-11-07, the frames: a thin smooth hoop at 1.5 and 5 km): wave_band_x 0.3 -> 0.5.
constexpr float wave_w_x = 0.1f, wave_band_x = 0.5f, wave_slab_x = 0.3f;
constexpr float wave_trail_span = 5.f; // the band in w_e
static_assert(wave_band_x - wave_trail_span * wave_w_x < 1e-4f && wave_trail_span * wave_w_x - wave_band_x < 1e-4f,
              "the band is the trail's span of w_e");
// v7 (2026-11-07, explosion-look.md D52 on; the user on the lab: a beige / olive ring with black soot blots, "not fire"):
// the wave is an annulus of the fireball's material. s = (r_t - d') / band, r_t the front shifted by the tongues: 0 the
// leading edge, 1 the band's end, the sheet beyond to s_end = wave_sheet_span / wave_trail_span (1 with the sheet off) as
// the eroded outer shell; rq = s / s_end the body's radial coordinate. The noise: three octaves (wave_noise_w) of a value
// noise periodic in the angle (wave_noise_cells about the circumference: no seam at the wrap) over s - wave_advect x
// (x = t / L_w, the material streaming back from the front) at wave_noise_radial cells a band, offset by the record's two
// seeds. The erosion as the body's: thr = wave_erode_0 + wave_erode_t x + wave_erode_r rq^2 (1 + wave_erode_rt x), d =
// saturate((n - thr) / wave_erode_soft), the heat's share h = saturate((n - thr) / wave_heat_soft). The lobes: rho = (1 +
// wave_lobe_amp (2 n_b - 1)) / (1 + wave_lobe_amp) of the break's mix n_b, lobe = saturate((rho - rq) / wave_lobe_soft).
// Density max(wave_floor_a (1 - smoothstep(1, s_end, s)), d lobe) x (1 - smoothstep(0.7, s_end, s)) x (ahead of the
// front the rim's fall-off, the noise held at s 0) x (wave_break_floor + (1 - wave_break_floor) m): inside the band never under the floor (dark red at
// wave_floor_t: no transparent hole). The rim: e^(-(z / wave_ahead)^2) ahead, 1 - smoothstep(0, wave_lead, s) behind, x m
// x (1 - wave_rim_noise + wave_rim_noise d), at T wave_t_edge (over wave_lead, 0.05 R_f). The material's T profile:
// wave_t_band0 at the front -> wave_t_band_end at the band's end, -> wave_t_sheet_end at the sheet's end; the
// material's T = wave_floor_t + (T_profile - wave_floor_t) h; the pixel's T the intensity-weighted mean of the rim's and
// the material's, x (1 - wave_t_cool x); intensity min(1, rim + density) (the slab never past the face-on brightness).
// Nothing ahead of the shifted front but the rim's fall-off (v6's sheet ran at full gain to the window and the strip's
// edge: the hard outer cut).
constexpr float wave_noise_cells[3] = {40.f, 86.f, 175.f}; // integers: periodic about the circumference
constexpr float wave_noise_radial[3] = {0.8f, 1.7f, 3.5f}; // cells a band at r = wave_noise_r_ref R_f (the body's sizes)
// The radial cells x sqrt(wave_noise_r_ref R_f / r): the cells about the circumference grow as r, the radial ones as
// sqrt(r); square at wave_noise_r_ref R_f (the fixed angular count alone streaked the young ring radially, 2:1 at 2 R_f)
constexpr float wave_noise_r_ref = 4.f;
constexpr float wave_noise_w[3] = {0.50f, 0.30f, 0.20f};   // the body's
constexpr float wave_advect = 1.0f;                        // band units over L_w
constexpr float wave_erode_0 = 0.2f, wave_erode_t = 0.2f, wave_erode_r = 0.5f, wave_erode_rt = 1.0f;
constexpr float wave_erode_soft = 0.3f, wave_heat_soft = 0.3f;
constexpr float wave_lobe_amp = 0.45f, wave_lobe_soft = 0.2f;
constexpr float wave_floor_a = 0.2f, wave_floor_t = 0.25f;
constexpr float wave_lead = 0.1f, wave_ahead = 0.7f, wave_break_floor = 0.6f, wave_rim_noise = 0.4f;
constexpr float wave_t_edge = 0.9f, wave_t_band0 = 0.55f, wave_t_band_end = 0.4f, wave_t_sheet_end = 0.25f;
constexpr float wave_t_cool = 0.5f, wave_e = 1.6f;
// The kill criterion's switches (2026-11-07 v3, the coordinator): wave_sheet_enabled false draws the wave as the thick
// textured ring (s_end 1: the material within the band); compose_enabled false draws every death body as the single lobed
// ball (with the fourth octave). Header constants, never settings.
// 2026-11-07 v4 (the kill criterion applied: the composed capital body read a smooth late lobe at 2700 / 3900 ms and a
// white blob at 0.2 L): compose_enabled false for the flight.
constexpr bool wave_sheet_enabled = true, compose_enabled = false;
// The eye near the plane (2026-11-07 v3: from above at 1.5 km the passing front was a full-screen white wall): within
// wave_eye_plane_x R_f of the plane (blended in over wave_eye_plane_blend_x R_f) the wave x smoothstep(0, wave_eye_fade_x
// R_f, the eye's in-plane distance from the band [r - band, r]): 0 inside the band, 1 a R_f outside it.
constexpr float wave_eye_plane_x = 2.f, wave_eye_plane_blend_x = 0.5f, wave_eye_fade_x = 1.f;
// The sheet behind the band (2026-11-07, the hoop's fix): out to wave_sheet_x R_f (wave_sheet_span w_e) behind the front.
constexpr float wave_sheet_x = 1.0f, wave_sheet_span = 10.f;
static_assert(wave_sheet_x - wave_sheet_span * wave_w_x < 1e-4f && wave_sheet_span * wave_w_x - wave_sheet_x < 1e-4f,
              "the sheet is its span of w_e");
// The flame tongues: the front's radius shifted by wave_tongue x the band (+-0.15 R_f) x (2 saturate(0.5 +
// wave_tongue_contrast (n_t - 0.5)) - 1), n_t = w_1 n_0 - w_0 n_1 + w_0 of the circumference break's two noises (wave_n_w,
// wave_n_cells: uncorrelated with the break's mix, mostly its finer noise; no further hash).
constexpr float wave_tongue = 0.3f, wave_tongue_contrast = 2.f;
// The window in z (w_e from the shifted front): the sheet inside, the rim's fall-off outside (e^(-(2.5 / 0.7)^2) ~ 3e-6);
// the drawn quads reach r - (window_in + tongue) w_e .. r + (window_out + tongue) w_e (wave_reach_in / _out), so no lit
// pixel lies outside them (v6 drew to r + 3.5 w_e while the tongues and the sheet lit to r + 5 w_e: a hard cut).
constexpr float wave_window_in = 12.f, wave_window_out = 2.5f, wave_light = 0.33f;
constexpr float wave_reach_in = wave_window_in + wave_tongue * wave_trail_span,
                wave_reach_out = wave_window_out + wave_tongue * wave_trail_span;
static_assert(wave_window_in + 1e-4f >= wave_sheet_span + wave_tongue * wave_trail_span && wave_window_out >= 2.f * wave_ahead,
              "the window holds the sheet behind the deepest tongue and the rim's fall-off ahead");
// The circumference's break (2026-11-07 [M]: at the hull ring's 0.5 and 2.5, 60 of 64 bins stayed above 10 % of the
// ring's peak): m = wave_m_floor + (1 - wave_m_floor) saturate(0.5 + wave_m_contrast (n - wave_m_bias)).
constexpr float wave_m_floor = 0.05f, wave_m_contrast = 4.f, wave_m_bias = 0.55f;
constexpr float wave_n_cells[2] = {9.f, 23.f}, wave_n_w[2] = {0.7f, 0.3f};
constexpr float wave_refract_px = 6.f, wave_refract_band = 2.f;
// The weights, a drawn screen of the strips (or the box at birth), 5120x1440 [M]: v6 1.40 / 1.24 (the sheet's second
// octave shed); v7 (the material, three octaves; the budget's step 0a sheds the sheet's third) re-measured below; the
// box's pixels past the window keep the early-out's 0.56 (that path unchanged).
constexpr float budget_weight_wave = 1.71f, budget_weight_wave_miss = 0.56f;
// The sheet with two octaves (the budget's step 0a).
constexpr float budget_weight_wave_one = 1.53f;
// wave_quads: a wave is drawn as the eight octagon strips of its annulus [r - window_in w_e, r + window_out w_e] (each
// an oriented screen quad about its sector, cut at the near plane and the screen), or as the slab's box when that is
// smaller (the birth: the annulus near a disc); the charge is the drawn quads' screens.
constexpr unsigned max_waves = 2, wave_strips = 8, wave_quads = 8;
constexpr unsigned wave_seed_element = 40; // element_hash(sequence, wave_seed_element + j): the circumference's seeds
constexpr unsigned compose_element = 42; // element_hash(sequence, compose_element): the composed body's N and jets
// Section 13.2, the composed body (every tier): a death body is compose_n sub-bursts of the burst impostor in one quad
// (weapon_fireball_ps.hlsl's composed branch; the table from the record's noise seed s by hash2, mirrored by
// stage::boom_compose_table and the lab): sphere i < N has radius (compose_r_lo + compose_r_span (1 - (i + h0) / N))
// (stratified, the largest first: the LOD drops the last), offset compose_offset h1 along unit(lerp(u_i, +-long axis,
// compose_axis_blend)) (u_i from h3, h4; alone without rows), birth compose_spread h2 L (sphere 0 on the star's frame),
// its clock x_i = (x - delta_i) / (1 - delta_i) so all end at L, radius and offset x rise(x_i) = r0 + (1 - r0)(1 -
// e^(-x_i / rho)) + body_grow x_i, each its own seeds (+ compose_seed_step i), erosion, lobes and white-hot core; jets
// (capital, station: compose_jets_lo..hi) a compose_jet_long : compose_jet_short spheroid along an axis within
// compose_jet_cone of the hull's side normal at a point (2 h1 - 1) compose_offset along the long axis, its centre
// compose_jet_long / 2 out, born compose_jet_birth0 + compose_jet_birth_span h2 of L. The densities compose as the union
// 1 - prod(1 - d_i); the hottest sphere's heat and core (weighted by its density) colour the pixel: overlaps never add.
// The quad is the cluster's bounding sphere (compose_box_margin); its unit compose_size x the drawn sphere / the CPU's
// projected outline (mean of compose_bins rays), so the cluster's 10 %-of-peak outline follows R(t) as the single body's.
// LOD (section 7 step 1): (1b) the shared-noise mode (one noise sample, the other spheres by chord and lobes), (1c)
// spheres down one at a time (the last first), then the jets, never under compose_n_min; under compose_small_px of
// R_f at 1440 rows N compose_n_min from birth; the eye inside the cluster: the shared mode and N at most compose_eye_n
// (jets first). Charge: the box x the fireball's weight x (compose_cost_base + compose_cost_per N), shared
// compose_cost_shared_base + compose_cost_shared_per N (the compose_factor rows: the program's cost per box screen over
// the single body's, 5120x1440, 2026-11-07 [M]: N 2 / 3 / 4 / 5 0.75 / 0.86 / 0.95 / 1.04, shared N 4 0.82).
constexpr unsigned compose_n_lo[5] = {3, 3, 4, 5, 3}, compose_n_hi[5] = {3, 4, 5, 5, 3};
constexpr unsigned compose_jets_lo[5] = {0, 0, 1, 1, 0}, compose_jets_hi[5] = {0, 0, 2, 2, 0};
// compose_eye_n 2 (the note's 4): the eye inside a fighter's cluster measured 336.5 us for the frame at N 3 shared
// against the 300 us fence (the body 283, the single body's 225) [M].
constexpr unsigned compose_n_min = 2, compose_eye_n = 2, compose_max = 7;
static_assert(compose_n_hi[3] + compose_jets_hi[3] <= compose_max, "the program's loop bound");
// The kill step "still a ball" applied (2026-11-07, the first native frames: the fighter's outline CV 0.144 / 0.166 at
// 0.3 / 0.6 L against 0.18, the single body's 0.143 [M]): offsets 0.5 -> 0.7, radii 0.45-0.8 -> 0.35-0.7.
// The offsets stratified (2026-11-07, the frames: smooth balloons; at 0.6 L one blob on every tier [M]): sphere i >= 1
// at compose_offset (compose_offset_min + (1 - compose_offset_min) h1), so the rims between the sub-bursts erode open
// into clumps by 0.6 L while they still join at 0.3 L.
// The outer sub-bursts burn cooler: heat x (1 - compose_offset_cool |offset| / compose_offset) (the rim's colour stays
// the single body's: an offset sub-burst's own hot centre lies in the cluster's rim).
constexpr float compose_r_lo = 0.35f, compose_r_span = 0.35f, compose_offset = 0.85f, compose_axis_blend = 0.5f;
constexpr float compose_offset_min = 0.7f, compose_offset_cool = 0.3f;
constexpr float compose_spread = 0.15f, compose_seed_step = 7.3f;
constexpr float compose_jet_long = 0.8f, compose_jet_short = 0.4f, compose_jet_cone = 0.78539816f;
constexpr float compose_jet_birth0 = 0.05f, compose_jet_birth_span = 0.1f;
// compose_size per tier (fighter, corvette, capital, station, missile): the capital's outline measured 1.051-1.053 of
// R(t) at 0.3 L with the stratified offsets against the 5 % of death_big_tier_sizes, the missile's 0.966 [M]: the capital's
// unit x 0.97.
constexpr float compose_size[5] = {1.f, 1.f, 0.97f, 1.f, 1.f};
constexpr float compose_box_margin = 1.1f, compose_small_px = 24.f;
// The outline's estimate per sub-burst (a sphere's outline its drawn radius over the drawn sphere's factor) x the single
// body's measured outline loss by age s(x) = 1 - compose_erode_k max(0, x - compose_erode_x0) (1.04 / 1.00 / 0.81 / 0.62
// of R(t) at 0.1 / 0.3 / 0.6 / 0.8 L [M]) at the sphere's erosion clock over the body's: a sub-burst eroding ahead of the
// body draws a smaller outline, so the unit grows to keep the cluster's on R(t).
// The loss counted at compose_erode_k 0.5 (2026-11-07 [M]: at the measured 0.76 the capital's and station's outlines
// read 1.061 / 1.048 of R(t) at 0.3 L, the big bodies hold their outline longer than the fighter's).
// compose_erode_cap 1 (counted to the end): capped at 0.4 the fighter's outline read 0.911 of R(t) at 0.3 L [M]; uncapped the lit
// share inside R(t) at 0.9 L reads 0.246 against the gate's 0.20 (open).
constexpr float compose_erode_x0 = 0.3f, compose_erode_k = 0.5f, compose_erode_cap = 1.0f;
// The CPU pass (2026-11-07: 32 deaths 64.2 us against 50): each record's placement at unit 1 (its rotated offsets,
// rise, erosion clocks and their slopes in the age) and the outline's estimate are refreshed only when the projected
// centre moved more than compose_refresh_px, every compose_refresh_frames builds, past compose_refresh_dx of the age,
// at a sub-burst's birth or with another set drawn; between refreshes each frame advances them linearly in the age (no
// exp or log: the radii never step at the refresh rate).
constexpr unsigned compose_refresh_frames = 4;
constexpr float compose_refresh_px = 2.f, compose_refresh_dx = 0.05f;
// Each sub-burst's erosion and heat clock 1 - (1 - x_i)^k, k = compose_pace_lo + compose_pace_span h5 (2026-11-07: the
// outline barely changed between 0.3 and 0.6 L with one shared clock, correlation 0.88-0.91 [M]).
// 1.0-2.4 (the frames, 2026-11-07: at 0.4 the slowest sub-bursts stayed dense and smooth to 0.6 L, balloons; at 0.8 the
// lit share inside R(t) at 0.9 L measured 0.258 against 0.20): no sub-burst erodes slower than the body.
constexpr float compose_pace_lo = 1.0f, compose_pace_span = 1.4f;
constexpr unsigned compose_bins = 16;
// The LOD by count (the CPU pass's fallback, 2026-11-07): above compose_count_3 live death bodies a frame each composes
// at most 3 sub-bursts, above compose_count_2 at most 2 (jets first).
constexpr unsigned compose_count_3 = 16, compose_count_2 = 24;
// The union and the core (2026-11-07, the frames: the union 1 - prod(1 - d_i) and one core per sub-burst on the body's
// clock drew large smooth saturated-white balloons from P + 800 ms): the densest sub-burst's density plus
// compose_soft_add of the others' (capped at 1: the radiance under E_b, overlaps never sum to white); each sub-burst's
// white-hot core core_gain (1 - x / compose_core_x) on the body's clock x: none past compose_core_x (the single body's
// core_x 0.35; a per-sub-burst age window drew a late sub-burst's white lamp at 0.2 L in the frames; at 0.2 the
// fighter's hue at 0.2 L fell to 0.50 against death_cools_through_ramp's 0.75 [M]: 0.25, a fifth of the gain left there).
constexpr float compose_soft_add = 0.25f, compose_core_x = 0.25f;
constexpr float compose_cost_base = 0.47f, compose_cost_per = 0.115f, compose_cost_shared_base = 0.70f,
                compose_cost_shared_per = 0.03f;
// The hull bursts' composition (R_b >= compose_hull_r_m, N 3): deferred (hull-burst-look.md section 10).
constexpr float compose_hull_r_m = 60.f;
} // namespace boom
// ---------------------------------------------------------------------------------------------- the handler (2.2)
// mode_shields_hull (step 2, docs/architecture/weapon-look.md 3.1 and 2.12): the shield hit as in shields mode plus the
// hull hit (family_hull). Both are the stage's modes (stage_mode): the ownership latch, the ring's scope and the stage.
// mode_shields_hull_bolts (step 4, docs/architecture/bolt-muzzle-look.md): the hull mode plus the bolts and the muzzle
// (family_bolts: the per-bullet tap 0x0045ac7d, the muzzle site 0x00446268 and the flight-effect skip 0x004146c0,
// weapon_effects_bolt.h); hull_mode() is true for both hull modes.
// mode_shields_hull_explosions / mode_shields_hull_bolts_explosions (step 3, docs/architecture/explosion-look.md and
// weapon-look.md 3.2): the hull modes plus the death explosion (family_boom: the two death sites 0x0044ac64 /
// 0x0044acb2, boom::decide); explosions_mode() is true for both, hull_mode() and stage_mode() too.
enum Mode : std::uint8_t { mode_native = 0, mode_observe = 1, mode_shields = 2, mode_shields_hull = 3,
                           mode_shields_hull_bolts = 4, mode_shields_hull_explosions = 5,
                           mode_shields_hull_bolts_explosions = 6 };
inline const char* mode_name(unsigned m) noexcept {
    return m == mode_observe                         ? "observe"
           : m == mode_shields                       ? "shields"
           : m == mode_shields_hull                  ? "shields,hull"
           : m == mode_shields_hull_bolts            ? "shields,hull,bolts"
           : m == mode_shields_hull_explosions       ? "shields,hull,explosions"
           : m == mode_shields_hull_bolts_explosions ? "shields,hull,bolts,explosions"
                                                     : "native";
}
inline bool stage_mode(unsigned m) noexcept {
    return m >= mode_shields && m <= mode_shields_hull_bolts_explosions;
}
inline bool hull_mode(unsigned m) noexcept {
    return m >= mode_shields_hull && m <= mode_shields_hull_bolts_explosions;
}
inline bool bolts_mode(unsigned m) noexcept {
    return m == mode_shields_hull_bolts || m == mode_shields_hull_bolts_explosions;
}
inline bool explosions_mode(unsigned m) noexcept {
    return m == mode_shields_hull_explosions || m == mode_shields_hull_bolts_explosions;
}
// X3M_WEAPON_EFFECTS (lower case): one word, "native", "observe" or "shields", or the family list "shields,hull",
// "shields,hull,bolts", "shields,hull,explosions" or "shields,hull,bolts,explosions" in any order ("hull,shields",
// "bolts,shields,hull"); spaces around each token are ignored. Unset or empty = native (note 2.6). Any other token
// ("beams": a later step), "hull", "bolts" or "explosions" without the families below them, an empty or repeated token
// is refused.
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, Mode* out) noexcept {
    static const char* const words[6] = {"native", "observe", "shields", "hull", "bolts", "explosions"};
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
        for (; w < 6; ++w) {
            std::size_t len = 0;
            while (words[w][len]) ++len;
            if (b - a != len) continue;
            bool same = true;
            for (std::size_t i = 0; i < len && same; ++i) same = text[a + i] == Char(words[w][i]);
            if (same) break;
        }
        if (w >= 6 || (seen & (1u << w))) return false; // an unknown, empty or repeated token
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
    if (tokens == 3 && seen == ((1u << 2) | (1u << 3) | (1u << 4))) {
        *out = mode_shields_hull_bolts;
        return true;
    }
    if (tokens == 3 && seen == ((1u << 2) | (1u << 3) | (1u << 5))) {
        *out = mode_shields_hull_explosions;
        return true;
    }
    if (tokens == 4 && seen == ((1u << 2) | (1u << 3) | (1u << 4) | (1u << 5))) {
        *out = mode_shields_hull_bolts_explosions;
        return true;
    }
    return false;
}
constexpr Mode default_mode = mode_native;
// "shields,hull,bolts,explosions" with spaces (16 until step 4, 32 until step 3)
constexpr unsigned setting_capacity = 48;

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
    // Step 3, the death sites (core::death): calls (every mode), records (census or ours), ours (the native spawn
    // skipped), the forwards by cause (no live node or class 0x1c, a bullet area death: step 7, the rule-(b) missile)
    // and the deaths recorded inside another death's game millisecond (nested kills of one outer call).
    std::uint32_t deaths = 0, death_records = 0, death_ours = 0, death_no_node = 0, death_bullet = 0, death_rule_b = 0;
    std::uint32_t death_nested = 0, death_tier[7] = {};
    // The big tiers (section 1): deaths forwarded through R', big-tier deaths forwarded native because R' was not ready
    // (or the stack was full), the restores after the call, a restore that found the column changed by the call (left
    // as the call left it), a restore with nothing saved, the columns Present restored itself (a swap whose call never
    // returned through the stub: an unwind).
    std::uint32_t death_swapped = 0, death_clone_missing = 0, death_swap_full = 0, swap_restores = 0, swap_changed = 0;
    std::uint32_t swap_underflow = 0, swap_leaked = 0;
    std::uint32_t clone_builds = 0, clone_invalidated = 0; // Present's R' builds (and rebuilds), the clones it found gone
    std::uint32_t clone_abandoned = 0; // slots Present left holding a clone (a changed or vanished source)
};
// One clone row R' (written by Present, read by the handler): the source row and the pre-roll it was built for, its
// slot, its element array and count (the handler checks both still sit in the slot before swapping), ready.
struct CloneRow {
    std::uint16_t source = 0, slot = 0;
    std::uint32_t preroll_ms = 0, elements = 0, count = 0;
    std::uint8_t ready = 0;
};
// One swapped death column: its address and the value the handler saved (restored LIFO after the forwarded call).
struct Swap {
    std::uintptr_t column = 0;
    std::uint32_t saved = 0, wrote = 0;
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
    // Step 3 (explosions modes): Present's latch beside hull_ready (the pass holds the death programs: the shock), and
    // the stage's three-frame floor for a death record (written by the stage; Present keeps an expired one that long).
    volatile std::uint8_t boom_ready = 0;
    std::uint32_t boom_floor_ms = 0;
    std::uint32_t death_ms = 0; // the game ms of the newest death record (flag_nested: another one in the same ms)
    // The big tiers' clone rows (Present writes them between frames, the handler reads them) and the swap stack (the
    // handler pushes, the restore entry pops; Present restores what an unwind left).
    CloneRow clones[boom::clone_rows]{};
    std::uint32_t clone_count = 0;
    Swap swaps[boom::swap_depth_max]{};
    std::uint32_t swap_depth = 0;
    Ring ring{};
    PosCheck check{};
    Counters counters{};
};
constexpr std::uint8_t family_shields = 1, family_hull = 2, family_bolts = 4, family_boom = 8;

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
    const bool hull_stage = hull_mode(s.mode) && s.owned_next && s.hull_ready && (s.armed & family_hull) &&
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

// Step 3, the death sites 0x0044ac64 (an area bullet, or any object with obj+0x40 & 0x40000000) and 0x0044acb2
// (everything else) -> 0x00413610(obj, killer, arg4) (docs/reverse-engineering/weapon-effect-spawn.md sections 4, 6, 8.1
// and 10). `esp` is the stack at stub entry ([1] the dying object, = EDI; [2] the killer, a pointer or an id [u]: never
// followed; [3] arg4), `ebp` the caller's EBP (0x0044abd0's frame: the reason word at [ebp+8]); `site` 0 for
// 0x0044ac64, 1 for 0x0044acb2. Integer only, plain loads and stores, no call. Returns death_skip (1) to skip the
// engine's spawn (the stage draws the death: boom::decide said ours and Present latched the stage and its death
// programs), death_swap (2) after writing the clone row R' into the type record's death column (the big tiers: the stub
// calls 0x00413610 and then death_restore, which writes the saved value back), death_forward (0) to forward.
// Forwarded without a record: outside the explosions modes, a null object, a missing node (or node+0x1c 0) or class
// 0x1c (the callee returns 0 at once), a bullet area death (class 0: step 7), and the rule-(b) missile (class 10 with
// the area flag and a non-zero killer: the callee itself makes no explosion). Every other death is recorded: the
// stage's (flag_stage; its native spawn skipped, or run through R' with flag_swapped | flag_forwarded) or census-only
// (flag_forwarded: the asteroid, any other class, a frame the stage did not own, a big tier whose R' is not ready: that
// death keeps the native look, cause_clone_not_ready). The swap: the column's value must be the source row of a ready
// clone built for the tier's pre-roll (or that clone's own slot: a nested death of the same type inside the call writes
// the same R' and restores what it saved), the slot must still hold the clone's count and array (a reload zeroes it),
// and the stack must have room; LIFO, so nested kills of other types touch their own records. The class is tested first
// at both sites (0x0044ac64 is taken for any object with the bullet flag, spawn note 4). The record retires the object's
// other live records first (its hits; a staged hull hit stays). Nested kills (an area blast's deaths inside one outer
// kill routine, spawn note 6) arrive as separate calls, each its own record; one in the same game millisecond as the
// previous death is marked flag_nested (census). A death record also carries the node's rows 0 and 2 (x 127: row 2 in
// dir, row 0 in alt_rgb332 / detail / calibre_q as int8) for the stage's hull frame, and its Cause in tier.
enum DeathResult : int { death_forward = 0, death_skip = 1, death_swap = 2 };
static inline __attribute__((always_inline)) int death(const std::uint32_t* esp, std::uint32_t ebp, unsigned site,
                                                       HookState& s) noexcept {
    ++s.counters.deaths;
    if (!explosions_mode(s.mode)) return 0;
    const std::uintptr_t obj = esp[1];
    const std::uint32_t killer = esp[2];
    if (!obj) return 0;
    const std::uintptr_t node = live_node(obj);
    const std::uint16_t cls = rd16(obj + layout::obj_class);
    if (!node || cls == layout::class_no_effect) {
        ++s.counters.death_no_node;
        return 0;
    }
    if (cls == layout::class_bullet) {
        ++s.counters.death_bullet;
        return 0;
    }
    const std::uint32_t flags = rd32(obj + layout::obj_flags);
    if (cls == layout::class_missile && (flags & layout::obj_missile_area) && killer) {
        ++s.counters.death_rule_b;
        return 0;
    }
    const std::int32_t radius = std::int32_t(rd32(obj + layout::obj_radius));
    const std::int32_t sub = std::int16_t(rd16(obj + layout::obj_subtype));
    std::uint8_t tier = boom::other, patch_tier = patch_fighter, tier_source = tier_by_radius;
    read_tier(obj, cls, radius, s.globals, &patch_tier, &tier_source);
    std::uintptr_t table_slot = 0, count_slot = 0;
    unsigned row_offset = 0;
    if (cls == layout::class_ship) {
        tier = patch_tier == patch_capital ? boom::capital : patch_tier == patch_corvette ? boom::corvette : boom::fighter;
        table_slot = s.globals.ship_table_slot;
        count_slot = s.globals.ship_count_slot;
        row_offset = layout::ship_death_row;
    } else if (cls == layout::class_dock || cls == layout::class_factory) {
        tier = boom::station;
        table_slot = cls == layout::class_dock ? s.globals.dock_table_slot : s.globals.factory_table_slot;
        count_slot = cls == layout::class_dock ? s.globals.dock_count_slot : s.globals.factory_count_slot;
        row_offset = layout::station_death_row;
    } else if (cls == layout::class_missile) {
        tier = boom::missile;
        patch_tier = patch_fighter; // the missile's U is the fighter's cell (its radius sits in the fighter band)
        table_slot = s.globals.missile_table_slot;
        count_slot = s.globals.missile_count_slot;
        row_offset = layout::missile_death_row;
    } else if (cls == layout::class_asteroid) {
        tier = boom::asteroid;
        table_slot = s.globals.asteroid_table_slot;
        count_slot = s.globals.asteroid_count_slot;
        row_offset = layout::asteroid_death_row;
    }
    // The type record's death row (the census cross-check; 0xffff unread): the subtype bounded by the class's count.
    std::uint16_t row = 0xffffu;
    std::int32_t class_id = 0;
    std::uintptr_t column = 0;
    if (table_slot) {
        const std::uintptr_t base = rd32(table_slot);
        const std::int32_t count = std::int32_t(rd32(count_slot));
        if (base && sub >= 0 && sub < count) {
            const std::uintptr_t rec = base + std::uintptr_t(sub) * layout::type_stride;
            column = rec + row_offset;
            row = std::uint16_t(rd32(column));
            if (cls == layout::class_ship) class_id = std::int32_t(rd32(rec + layout::ship_class_id));
        }
    }
    const boom::Decision decision = boom::decide(tier);
    const bool latched = s.owned_next && s.boom_ready && (s.armed & family_boom);
    std::uint8_t cause = decision == boom::decide_native ? boom::cause_tier_native
                         : !latched                      ? boom::cause_not_owned
                         : decision == boom::decide_ours ? boom::cause_ours
                                                         : boom::cause_swap;
    // The big tiers: the clone built for this column's row and the tier's pre-roll (or its own slot, when a death of the
    // same type is nested inside the forwarded call), still in its slot.
    std::uint32_t clone_slot = 0;
    if (cause == boom::cause_swap) {
        const std::uint32_t value = column ? rd32(column) : 0xffffffffu;
        const std::uint32_t pre = boom::preroll_ms(tier);
        if (!column) {
            cause = boom::cause_no_record;
        } else {
            for (std::uint32_t k = 0; k < s.clone_count && k < boom::clone_rows; ++k) {
                const CloneRow& c = s.clones[k];
                if (!c.ready || c.preroll_ms != pre || (value != c.source && value != c.slot)) continue;
                const std::uintptr_t slot = s.globals.effects_rows + std::uintptr_t(c.slot) * layout::row_stride;
                if (rd32(slot + layout::row_count) == c.count && rd32(slot + layout::row_elements) == c.elements)
                    clone_slot = c.slot;
                break;
            }
            if (!clone_slot)
                cause = boom::cause_clone_not_ready;
            else if (s.swap_depth >= boom::swap_depth_max)
                cause = boom::cause_swap_full;
        }
        if (cause != boom::cause_swap) {
            clone_slot = 0;
            if (cause == boom::cause_swap_full)
                ++s.counters.death_swap_full;
            else
                ++s.counters.death_clone_missing;
        }
    }
    const bool ours = cause == boom::cause_ours, swapped = cause == boom::cause_swap;
    const std::uintptr_t cfg = rd32(s.globals.cfg_slot);
    const std::uint32_t now = cfg ? rd32(cfg + layout::cfg_game_ms) : 0;
    const bool nested = s.counters.death_records && now == s.death_ms;
    ring_retire_owner(s.ring, std::uint32_t(obj), true);
    Event* e = ring_acquire(s.ring);
    if (!e) return death_forward; // forwarded unswapped: nothing would draw the death
    e->role = role_explosion;
    e->style = tier;
    e->tier = cause;
    e->flags = std::uint8_t((ours ? flag_stage : swapped ? (flag_stage | flag_forwarded | flag_swapped) : flag_forwarded) |
                            (nested ? flag_nested : 0));
    e->table = std::uint8_t(cls < 0x100 ? cls : 0xff);
    e->tint_source = std::uint8_t(site);
    e->row = row;
    e->t0_game_ms = now;
    e->lifetime_ms = ours || swapped ? boom::record_life_ms[tier] : 0;
    e->tint_rgb = std::uint32_t(class_id);
    // the node's rows 2 and 0 (local z and x in world), the stage's hull frame (the capsule's axis, the box's rotation)
    {
        std::int32_t r2[3], r0[3];
        read3(node + layout::node_rot + 32, r2);
        read3(node + layout::node_rot, r0);
        std::int8_t q0[3];
        quantize_dir(r2, e->dir);
        quantize_dir(r0, q0);
        e->alt_rgb332 = std::uint8_t(q0[0]);
        e->detail = std::uint8_t(q0[1]);
        e->calibre_q = std::uint8_t(q0[2]);
    }
    e->damage = std::int32_t(killer);
    e->owner = std::uint32_t(obj);
    e->node = std::uint32_t(node);
    e->handle = rd32(node + layout::node_handle);
    e->shield_q = ebp ? rd16(ebp + layout::caller_reason) : 0;
    e->patch_tier = patch_tier;
    e->tier_source = tier_source;
    read3(node + layout::node_pos, e->pos); // world (the sector's frame): the node dies with the object
    e->radius = radius;
    s.death_ms = now;
    ++s.counters.death_records;
    ++s.counters.death_tier[tier];
    if (nested) ++s.counters.death_nested;
    if (ours) ++s.counters.death_ours;
    if (!swapped) return ours ? death_skip : death_forward;
    // The swap: save the column, write R' (plain integer stores), the stub forwards and calls death_restore.
    Swap& w = s.swaps[s.swap_depth];
    w.column = column;
    w.saved = rd32(column);
    w.wrote = clone_slot;
    *reinterpret_cast<volatile std::uint32_t*>(column) = clone_slot;
    ++s.swap_depth;
    ++s.counters.death_swapped;
    return death_swap;
}
// The swap's second half (the stub's call after 0x00413610 returned): the newest saved column back, unless the call
// changed it from what the handler wrote (then it is left as the call left it: swap_changed). Integer only, no call.
static inline __attribute__((always_inline)) void death_restore(HookState& s) noexcept {
    if (!s.swap_depth || s.swap_depth > boom::swap_depth_max) {
        ++s.counters.swap_underflow;
        return;
    }
    const Swap& w = s.swaps[--s.swap_depth];
    volatile std::uint32_t* column = reinterpret_cast<volatile std::uint32_t*>(w.column);
    if (*column == w.wrote) {
        *column = w.saved;
        ++s.counters.swap_restores;
    } else
        ++s.counters.swap_changed;
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
// A burst class's lobed ember (the glow program's lobe branch, review 2026-11-07): ember_lobed_alone_fills_frame 89.7 us
// at 5120x1440 against the round glow's 70.8 (native fixture 2026-11-07).
constexpr float budget_weight_glow_lobed = 0.51f;
// The fireball impostor (2.14; the hull hit's hot-gas burst, docs/architecture/hull-burst-look.md section 2 since
// 2026-11-07; the explosions of step 3 at full size): a ray-sphere impostor (the chord's closed-form column of
// rho(r) = 1 - r^2 / R^2, clipped at the lane) shaded at the chord's middle q (normalised by the sphere's R_s) in the
// view-plane disc p = rot(theta) q.xy: three octaves of 2-D value noise n (fireball_octave_freq cells per R_s, weights
// fireball_octave_w, advected outward by fireball_advect cells over the life under the injective drift law of
// weapon_fireball_ps.hlsl: octave i's radius u = |p| f_i cells sampled at u - a y / sqrt(1 + y^2), y = u / 2a), alpha-eroded
// by the threshold
// thr = erode_0 + erode_t x + erode_r |q|^2 (1 + erode_rt x) (density saturate((n - thr) / erode_soft) x column^column_pow x the lobed
// silhouette: rho(phi) = (1 + lobe_amp (2 n_L - 1)) / (1 + lobe_amp), n_L a periodic 1-D value noise of fireball_lobes
// cells about the disc, edge fireball_lobe_soft); heat T(t) x (rim_t + (1 - rim_t) m), m = column x saturate((n - thr) /
// heat_soft), radiance heat(T') x E x density. fireball_octaves is the program's count; a burst's third octave is
// weighted in over fireball_fine_px .. + fireball_fine_band_px of its projected radius (at 1440 rows; review 2026-11-07:
// no step as a burst grows or shrinks through it), and the budget's shed step (1) takes it from every burst (never the
// second): entered when the charged screens pass the budget, left only after fireball_octave_hold frames and under
// fireball_octave_return of it (no frame-to-frame flip at the budget's edge).
constexpr float fireball_octave_freq[3] = {1.6f, 3.4f, 7.0f}; // noise cells per R_s
constexpr float fireball_octave_w[3] = {0.50f, 0.30f, 0.20f};
// Against the note's numbers (2026-11-07, docs/verification/weapon-effects.md "Burst body, glow, flash, smoke"): the
// note's erode_0 0.25, erode_soft 0.12, heat_soft 0.35, advect 1.5 and an age-free radial term failed the erosion and
// colour gates on most hits (a numpy model of this law over 12 hits and the fixture agree); erode_rt couples the
// radial term to age (the rim goes first, the note's "rising with age and radius"). The drift law (2026-11-07,
// docs/verification/weapon-effects.md "Fireball drift law"): the old constant shift's collapse had kept a hot dot at the
// centre late in the life; without it the bodies died on more seeds at 0.95 L (the plasma globe gate) and kept more rim
// at 0.9 L (the death body's lit share 0.200 -> 0.258 at erode_t 0.28): erode_t 0.30 -> 0.28, erode_soft 0.20 -> 0.28 and
// column_pow 0.35 -> 0.25 (a softer erosion edge lowers the dim rim's lit share without changing which pixels survive;
// the lower column power keeps the limb out to the size law at 0.3 L).
constexpr float fireball_advect = 1.0f; // outward drift of the pattern over the life (noise cells)
constexpr float fireball_erode_0 = 0.10f, fireball_erode_t = 0.28f, fireball_erode_r = 0.20f, fireball_erode_soft = 0.28f;
constexpr float fireball_erode_rt = 3.0f;
constexpr float fireball_column_pow = 0.25f;
constexpr float fireball_lobes = 5.f, fireball_lobe_amp = 0.25f, fireball_lobe_soft = 0.06f;
constexpr float fireball_heat_soft = 0.20f, fireball_rim_t = 0.45f;
// fireball_octaves 4 (2026-11-07, the capital's 300-400 px lobes held no noise at that scale): a body or burst past
// fireball_fine4_px of projected radius (1440 rows) blends a fourth octave in over fireball_fine4_band_px
// (fireball_octave_freq4 cells per R_s at fireball_octave_w4; disc.w 1 .. 2, the frame's octaves 4); the budget sheds it
// first (octaves 3), then the third (octaves 2).
constexpr unsigned fireball_octaves = 4;
constexpr float fireball_fine_px = 48.f; // under this projected radius (px at 1440 rows) a burst draws two octaves
constexpr float fireball_fine_band_px = 16.f; // the third octave's weight 0 -> 1 over this much more radius
constexpr float fireball_fine4_px = 160.f, fireball_fine4_band_px = 60.f;
constexpr float fireball_octave_freq4 = 14.f, fireball_octave_w4 = 0.25f;
constexpr float fireball_octave_return = 0.8f;
constexpr unsigned fireball_octave_hold = 3;
// One fireball rectangle filling the frame at 5120x1440 with every disc pixel taking the noise, EVENT-fenced (native
// fixture 2026-11-07 before the lobes' early-out: burst_alone_fills_frame 210.2 us, _coarse at two octaves 182.2,
// smoke_alone_fills_frame 213.5), over the decal's 177.5 us: the worst case. The alone rows read 0.94 / 0.64 / 0.58 since
// the lobes skip their outside, but a dense close-capital frame measured 0.73 per raster screen at two octaves
// (docs/verification/weapon-effects.md "Burst body, glow, flash, smoke").
constexpr float budget_weight_fireball = 1.18f, budget_weight_fireball_coarse = 1.03f, budget_weight_smoke = 1.20f;
// A screen of bodies with the fourth octave (2026-11-07; review: measured on a body seen from outside, the eye at 2 R:
// 27.4 against 27.6 us with three, 0.309 screens [M], the fourth's cost under the noise; an eye-inside body keeps three,
// where it cost 1.35 a screen): pinned at the three-octave weight.
constexpr float budget_weight_fireball_fine4 = 1.18f;
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
// Flash (hull-burst-look.md section 4, 2026-11-07): a four-arm star (the sprite batch's star kind) at the angle
// (pi / 2) unit(hash(seq, star_element)), arms star_arm_len x A along +u, +v, -u, -v, each (1 - s)^2 along it across
// a width star_width A (1 - s) + star_width_px, a white disc core (>= floor_flash_px) at core_e, the arms the colour
// source at core_e; no halo. Columns: core r, arm A r, life ms, arm heat, arm colour (0 heat, 1 the tint when
// saturated (plasma), 2 ion_colour (fixed, ratified 2026-11-06), 3 the missile tint, 4 cool_colour (repair, at its
// pulse start)). A zero life: no flash. The heavy class adds the entry streak (streak_*).
constexpr float class_flash[10][5] = {{0.12f, 1.2f, 40.f, 0.9f, 0.f}, {0.12f, 0.6f, 40.f, 1.0f, 0.f},
                                      {0.12f, 2.0f, 40.f, 1.0f, 1.f}, {0.12f, 1.0f, 40.f, 0.9f, 0.f},
                                      {0.12f, 1.2f, 40.f, 1.0f, 2.f}, {0.12f, 1.2f, 40.f, 0.9f, 0.f},
                                      {0.12f, 1.5f, 40.f, 0.9f, 3.f}, {0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.12f, 0.6f, 40.f, 0.f, 4.f}, {0.f, 0.f, 0.f, 0.f, 0.f}};
constexpr float star_arm_len[4] = {1.0f, 0.55f, 0.8f, 0.45f};
constexpr float star_width = 0.10f, star_width_px = 2.f;
constexpr float star_floor_px = 6.f; // the arm A never below this (px at the target's rows): a star, not a dot
// the per-hit hashes (the star's its own since 2026-11-07: 9 is the shimmer rect's seed)
constexpr unsigned star_element = 11, burst_rot_element = 8, burst_lobe_element = 10;
// Puff (the fireball impostor riding the owner): R_b = rb_r r + U (rb_u + rb_uk k), L = life + life_k k ms, heat T0 -> 0
// over L, body E (<= look::body_max), R(t) = R_b (1 - e^(-t / (rise L))) x (1 - shrink max(0, (t / L - 0.3) / 0.7)), the
// tint mixed in at tint_mix (1 - t / L) when saturated (plasma). Columns: rb_r, rb_u, rb_uk, life, life_k, T0, E, rise,
// shrink, tint_mix. A zero radius: no puff.
// R_b doubled 2026-11-07 (hull-burst-look.md 2.6: the body fills the hit's footprint instead of the glow's lamp). The
// asteroid's (2026-11-07 addendum): the unlit dust cloud, dust_colour at E 0.375 (a quarter of the energy body's) with
// the same erosion, rising faster (rise 0.08) so the cloud reads at 120 ms.
constexpr float class_puff[10][10] = {{2.0f, 0.f, 0.f, 350.f, 150.f, 1.0f, 1.5f, 0.15f, 0.f, 0.f},
                                      {0.8f, 0.f, 0.f, 200.f, 0.f, 0.6f, 0.8f, 0.15f, 0.f, 0.f},
                                      {3.0f, 0.f, 0.f, 600.f, 400.f, 1.0f, 1.5f, 0.2f, 0.5f, 0.5f},
                                      {1.6f, 0.f, 0.f, 300.f, 200.f, 1.0f, 1.5f, 0.1f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
                                      {1.2f, 0.f, 0.f, 300.f, 0.f, 1.0f, 1.5f, 0.15f, 0.f, 0.f},
                                      {0.f, 2.0f, 2.0f, 500.f, 700.f, 1.0f, 1.5f, 0.15f, 0.f, 0.f},
                                      {2.4f, 0.f, 0.f, 800.f, 400.f, 0.3f, 0.375f, 0.08f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}};
// Spark sets, three per anatomy (row 3 a + set: 0 fast, 1 slow, 2 chips): n = n0 + nk k (x the count's spread), speed U/s
// (1 +- speed_spread), life + life_k k ms (x the spark's factor), width U, rest length in widths, tau s, radiance E at
// birth (x its life fraction f), heat T0 f, the shot-axis bias b (v = speed normalize(u + b a), a = Event::dir in world;
// b 0 isotropic, and without dir), duty: the share of flicker_ms slots it is lit in (0: always). Columns: n0, nk, speed,
// speed_spread, life, life_k, width, rest_w, tau, E, T0, bias, duty. n0 = nk = 0: no set. The beam's slow row is its
// spatter: n0 sparks per spatter_period_ms while held. The ion has no sparks (its arc sparks dropped 2026-11-07: less
// dense, less chaotic; the lightning alone).
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
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // ion: no sparks (2026-11-07: the lightning alone)
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // beam: the spatter (slow)
    {2.f, 0.f, 4.f, 0.4f, 300.f, 0.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {16.f, 24.f, 8.f, 0.3f, 150.f, 50.f, 0.08f, 3.f, 0.45f, 8.f, 1.f, -0.3f, 0.f}, // missile: two debris sets
    {8.f, 12.f, 3.f, 0.3f, 1500.f, 0.f, 0.08f, 3.f, 1.0f, 8.f, 1.f, -0.3f, 0.3f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // asteroid: rock dust and chips
    {8.f, 8.f, 3.f, 0.3f, 450.f, 150.f, 0.08f, 3.f, 0.45f, 0.75f, 0.3f, 0.f, 0.f}, // dimmer, in dust_colour
    {6.f, 6.f, 2.f, 0.5f, 600.f, 200.f, 0.12f, 1.5f, 1.0f, 0.75f, 0.25f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // repair
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // tug
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}};
// The burst's glow (hull-burst-look.md section 3, 2026-11-07): every class with a burst but the asteroid's dust lights
// the hull with the glow primitive of R_g = glow_r_x R(t) (the burst's radius now), E_g = glow_e_x E_b, colour
// heat(T(t)) E_g T(t), over the burst's life (its stagger hash, element 3). Per owner and frame its live burst records
// within cluster_r_x R_b of a record (N = 1 + the others' remaining life fractions, at most 64: a neighbour fades out of
// N, review 2026-11-07) scale its glow by 1 / (1 + (N - 1) / cluster_n0_glow) and its body by 1 / (1 + (N - 1) /
// cluster_n0_burst).
constexpr float glow_r_x = 1.6f, glow_e_x = 0.33f; // glow_r_x 1.5 in the note; 1.6 by the glow_follows_burst gate
// cluster_n0_burst 3.0 in the note; 2.0 by the hull_20_hits_no_white_sun gate (3.29 linear measured at 3.0); 1.6 since
// the neighbours count by their remaining life (review 2026-11-07: 3.03 measured at 2.0, the hard count 2.45)
constexpr float cluster_r_x = 2.0f, cluster_n0_glow = 2.5f, cluster_n0_burst = 1.6f;
// Smoke (hull-burst-look.md section 5): the burst's impostor drawn in the scorch pass (multiply), over the lane's
// surface only, riding the owner, from smoke_t0 L_b over smoke_span L_b: radius R over R_b x R_b, the burst's noise,
// seed and rotation, the erosion over its age y, strength s0 min(1, y / smoke_rise) (1 - y) / (1 - smoke_rise) x
// scorch_colour (its peak s0 at smoke_rise, 0.8 L_b: the note's "at s0 0.5 the hull reads at 50 %"). Columns:
// R over R_b, s0. A zero radius: none (ion, repair, tug; the asteroid's dust takes the ballistic dust's, addendum).
constexpr float class_smoke[10][2] = {{1.3f, 0.45f}, {1.0f, 0.30f}, {1.2f, 0.50f}, {1.3f, 0.50f}, {0.f, 0.f},
                                      {1.2f, 0.40f}, {1.3f, 0.50f}, {1.0f, 0.30f}, {0.f, 0.f},   {0.f, 0.f}};
constexpr float smoke_t0 = 0.5f, smoke_span = 1.1f, smoke_rise = 0.27f;
// The asteroid's rock dust (its body, its sparks: unlit, greyer than hull sparks) and the cool spot of repair and tug.
constexpr float dust_colour[3] = {0.55f, 0.52f, 0.48f};
constexpr float cool_colour[3] = {0.7f, 0.8f, 1.0f};
// Glow (the glow primitive at the hit; since 2026-11-07 repair's and tug's only, the burst classes take the burst's
// glow above): R r, life ms (the fall after the hold), heat T, radiance E (repair and tug: cool_colour, at E: 0.2 and
// 0.1 of the energy body's 1.5). Ember (the post-hit light): R r, life + life_k k ms, heat T0
// cooling to 0, E (radiance ramp(T) x E x T; repair: the tint x E fading); the beam's is its spots' trail ember.
constexpr float class_glow[10][4] = {{0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f},
                                     {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f},
                                     {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}, {1.f, 150.f, 0.f, 0.3f},
                                     {1.f, 150.f, 0.f, 0.15f}};
constexpr float class_ember[10][5] = {{0.8f, 1500.f, 500.f, 0.6f, 0.8f}, {0.3f, 500.f, 0.f, 0.4f, 0.5f},
                                      {1.0f, 3000.f, 0.f, 0.6f, 1.0f},   {0.5f, 2000.f, 0.f, 0.7f, 1.2f},
                                      {0.f, 0.f, 0.f, 0.f, 0.f},         {0.8f, 1500.f, 0.f, 0.6f, 0.8f},
                                      {1.0f, 2000.f, 0.f, 0.6f, 1.0f},   {0.f, 0.f, 0.f, 0.f, 0.f},
                                      {0.4f, 800.f, 0.f, 0.f, 0.6f},     {0.f, 0.f, 0.f, 0.f, 0.f}};
// The ember of a class with a burst takes the burst's lobed silhouette (review 2026-11-07: the late ember read as a round
// lamp): its radius x rho(phi) = (1 + ember_lobe_amp (2 n_L - 1)) / (1 + ember_lobe_amp), the burst's own lobe noise
// (look::fireball_lobes cells, its seed and rotation), so it is the body's clumped remnant.
constexpr float ember_lobe_amp = 0.25f; // look::fireball_lobe_amp, the body's
// Ring classes (a class_ring row): the burst's glow reaches the ring's drawn radius r(t) + ring_front_window w while the
// ring lives, released with it: R_g = glow_r_x R(t) + max(0, that - glow_r_x R(t)) (1 - t / L_ring) (review 2026-11-07:
// the body, glow and ring read as one event, no dark annulus between them).
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
// Ion: the lightning (2026-11-07, user: "less dense / chaotic, more lightning like"; weapon_filament_ps.hlsl, one
// segment quad each). Per hit ion_channels_lo..hi main channels (hashed on the record's sequence), each a jagged
// polyline of ion_segments_lo..hi segments in the hull's tangent plane at the hit (the normal the lane's depth gives at
// the contact pixel, weapon_lightning_vs.hlsl; the contact point's direction from the owner's origin where the lane
// cannot: the contact off screen, under another surface, or no vertex texture fetch of the lane's format), ion_len r
// long x (1 +- ion_len_spread / 2), its directions spread about
// the hit (base hashed, channel c at c x 360 / count degrees, +- 2 atan(ion_dir_jitter)); each segment steps
// ion_jag x its length sideways at most (clamped to ion_lateral x the channel's length); per channel ion_branches_lo..hi
// side branches from an inner vertex at 30..60 degrees (tangents of their half angles ion_branch_t_lo..hi), ion_branch_len
// x its length, half its segments (at least 3), ion_branch_e x its radiance. The lane under each segment's projection
// within ion_depth_r r of its depth carries it (never the sky). Profile across a segment (pixels, d from it): the core
// saturate(hw + 1/2 - d) of half width hw = max(ion_core_w r, ion_core_px x H / 1440, 1) / 2 (branches ion_branch_px),
// white mixed ion_core_white towards ion_colour, under it the corona ion_corona_share x saturate(1 - (d / hc)^2) in
// ion_colour, hc = ion_corona_x hw. Timing (game ms from the first drawn frame, no stagger; 2026-11-07, user: "more
// lightning, more lingering, and a light on the hull"): strike j starts at t_j (t_0 = 0, t_j+1 = t_j + P_j, the period
// P_j hashed in ion_period_lo_ms..ion_period_hi_ms and at least the three-frame floor) while t_j < ion_strike_until x
// ion_life_ms (5 to 8 strikes), its channels re-seeded; it flashes for max(ion_strike_ms, the floor) at ion_channel_e x
// e_j (e_j = 1 - ion_strike_decay j for a strike starting before ion_late_ms, else ion_late_e, its widths x ion_late_w),
// then lingers as the afterglow at ion_after_e x e_j for one re-strike period (to the life's end for the last strike), so
// strike j's flash draws over strike j - 1's afterglow. Everything x the life's fade: 1 to ion_fade_from x ion_life_ms,
// then linear to 0 at ion_life_ms (no hard cut). The cold light on the hull: the glow primitive at the hit in ion_colour
// (never the heat ramp), radius ion_glow_r x the latest strike's extent (its farthest point from the hit), radiance
// ion_glow_e x e_j x (ion_glow_floor + (1 - ion_glow_floor) (1 - x)) x the fade, x the share of strike j's period gone
// (peak at each strike, decaying over its period). No heat elements (no puff, heat glow, ember, scorch) and no sparks.
// Kill switch for a web: both ion_branches_lo and ion_branches_hi at 0 (no branches; hull-impact-classes.md section 9).
constexpr unsigned ion_channels_lo = 3, ion_channels_hi = 6, ion_segments_lo = 6, ion_segments_hi = 10;
constexpr unsigned ion_branches_lo = 1, ion_branches_hi = 3;
constexpr float ion_len = 1.0f, ion_len_spread = 0.5f, ion_dir_jitter = 0.27f, ion_jag = 0.8f, ion_lateral = 0.3f;
constexpr float ion_branch_t_lo = 0.2679f, ion_branch_t_hi = 0.5774f, ion_branch_len = 0.5f, ion_branch_e = 0.6f;
constexpr float ion_depth_r = 1.0f;
constexpr float ion_core_w = 0.01f, ion_core_px = 2.0f, ion_branch_px = 1.0f, ion_corona_x = 4.0f;
constexpr float ion_corona_share = 0.25f, ion_core_white = 0.7f;
constexpr float ion_channel_e = 3.0f, ion_strike_decay = 0.15f, ion_after_e = 0.25f, ion_late_w = 0.6f, ion_late_e = 0.5f;
constexpr unsigned ion_life_ms = 1200, ion_period_lo_ms = 120, ion_period_hi_ms = 200, ion_strike_ms = 40, ion_late_ms = 200;
constexpr float ion_strike_until = 0.8f, ion_fade_from = 0.8f;
constexpr float ion_glow_r = 1.2f, ion_glow_e = 0.3f, ion_glow_floor = 0.25f;
constexpr unsigned flicker_ms = 40;
// The strike span (ms; a held ion beam's strike cycle) and the most strikes a life holds (every period at its shortest).
constexpr unsigned ion_strike_span_ms = unsigned(ion_strike_until * float(ion_life_ms));
constexpr unsigned ion_strikes_max = (ion_strike_span_ms + ion_period_lo_ms - 1) / ion_period_lo_ms;
constexpr float ion_colour[3] = {0.60f, 0.75f, 1.00f};
// The lightning's reach from the hit (x r): a channel's longest (ion_len (1 + ion_len_spread / 2)) with its lateral
// clamp, a branch from its end (ion_branch_len of that again).
constexpr float ion_reach_r = ion_len * (1.f + 0.5f * ion_len_spread) * (1.f + ion_lateral) * (1.f + ion_branch_len);
// Beam (2026-11-07, user: "the beam is light"): the held spot's halo and its glow take the weapon colour (event_tint:
// the light colour when saturated, else the texture average, else the override or white), white mixed beam_glow_mix
// towards it; the spot's core beam_core_mix (a green beam's spot a green-white light); the glow keeps the heat glow's
// mean radiance. Only where a colour exists (review 2026-11-07): the record's tint_source a light or a texture colour
// whose 8-bit saturation (max - min) / max is above beam_colour_sat_min; a white or grey beam (Mayhem 3's PBC: a white
// light, no texture; stock PAL, FUSIONBEAM, TRIBEAM: grey lights) keeps the heat halo and glow. Everything heated stays
// on the heat ramp: the pulse star, the boiling puff, the smoke, the spatter, the trail ember and the scorch line. Repair
// and tug keep cool_colour.
constexpr bool beam_colour = true;
constexpr float beam_glow_mix = 0.6f, beam_core_mix = 0.3f, beam_colour_sat_min = 0.15f;
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
// The shared budget's weights of the new primitives (2.11, over the decal's 177.5 us at 5120x1440; the native fixture's
// *_alone_fills_frame rows, 2026-11-06: the ring 71.6 us, the filament glow 153.2, the scorch 86.7, a flash's halo
// holding the eye 87.3; the ring as a shock front 88.1-90.5 us, 2026-11-07; the star 71.7, 2026-11-07): charged; the
// flashes are the shed order's last step (other owners', oldest first). budget_weight_filament charges the ion's
// lightning segments (their quads' screens): the lightning program on every pixel measured 87.9 us at 5120x1440 (0.50,
// 2026-11-07), the filament glow's 0.86 kept as its bound.
constexpr float budget_weight_ring = 0.50f, budget_weight_filament = 0.86f, budget_weight_scorch = 0.49f;
constexpr float budget_weight_flash = 0.40f;
// A kept hull shimmer rect (with its ring's band) per screen of its drawn rect over the decal's 177.5 us per screen:
// run_engine_shimmer.py --host native 2026-11-07, two rings at the cap's reach (WEAPON_RING_TIMING) 245.4 us for 0.486
// screens of boxes at 5120x1440 (2.84; a plume rect of a tenth 44.2 us, 2.49).
constexpr float budget_weight_shimmer = 2.85f;
constexpr unsigned max_sparks = 2048;   // spark quads per frame (2.11)
constexpr unsigned max_owners = 32;     // owners re-posed per frame
} // namespace hull
static_assert(max_owners + hull::max_owners + boom::max_bodies <= VisibleOwners::capacity,
              "every owner a stage frame can draw");
// The view volume's sphere about a shield stage owner (stage::owner_in_view): its object radius (obj+0xa4) x this, past
// the bubble (bubble_scale), so an owner just past the screen edge whose bubble still reaches into it counts as drawn.
// The hull stage judges its owners by their reach sphere instead (stage::out_of_reach: 2 r + the records' element reach,
// grown by the pixel floors), the same test its cull uses.
constexpr float visible_radius_scale = 2.0f;
// A hull hit's record life per anatomy (review 2026-11-07): its slowest element (an ember, a puff, a glow, a ring, the
// flash, the streak, the ion's lightning to ion_life_ms (no stagger), a spark set at spark_life_hi; the scorches live in
// their own store, the beam, repair and tug spots in theirs), so each class leaves the ring at its own end;
// hull_lifetime_ms the slowest of them (the installed bound). Present keeps a record life_floor_ms longer. Compile-time
// integers: the handler reads the table, never a float.
constexpr float hull_slowest_ms(unsigned a) noexcept {
    const float st = 1.f + 0.5f * stagger;
    float m = 0.f;
    const float burst = (hull::class_puff[a][3] + hull::class_puff[a][4]) * st;
    const float cand[8] = {(hull::class_ember[a][1] + hull::class_ember[a][2]) * st, burst, hull::class_glow[a][1] * st,
                           hull::class_smoke[a][0] > 0.f ? (hull::smoke_t0 + hull::smoke_span) * burst : 0.f,
                           hull::class_ring[a][2] * st, hull::class_flash[a][2] * st,
                           a == hull::an_heavy ? float(hull::streak_ms) * st : 0.f,
                           a == hull::an_ion ? float(hull::ion_life_ms) : 0.f};
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

// ---------------------------------------------------------------------------------------------- bolts and muzzle (step 4)
// docs/architecture/bolt-muzzle-look.md section 8 (ratified 2026-11-07): the look's header constants, read by name by
// tools/effects/generate_weapon_effects_lab.py (HEADER.bolt, HEADER.muzzle) and re-pinned by the fixture. Styles index
// 0 tracer, 1 slug, 2 orb, 3 wave, 4 heavy (the heavy class modifies a tracer or slug: k >= heavy_k); w is the bolt width
// in metres (section 1), radiance linear before the TAA and AgX. mode full | simple (the kill fallback: core + body + halo,
// no tail, no rim, c_s 0.5, orb smooth) is a header decision, never a setting.
namespace bolt {
enum Mode : std::uint8_t { full = 0, simple = 1 };
constexpr Mode mode = full;
enum Style : std::uint8_t { tracer = 0, slug = 1, orb = 2, wave = 3, heavy = 4, style_count = 5 };
// Section 1, the width law: w = (w_px_target x d_eng / f_ref) x (1 + w_k k) m, d_eng = min(d_eng_m[tier], d_eng_range x
// range); f_ref the pixels per metre at 1 m at ref_rows rows and the vanilla vertical FOV 73.74 deg (field-of-view.md):
// (1440 / 2) / tan(36.87 deg) = 960. w never above w_cap_m (the capital kill step's first lever).
constexpr float d_eng_m[3] = {600.f, 1500.f, 4000.f}; // fighter, corvette, capital mounts
constexpr float d_eng_range = 0.75f;
constexpr float w_px_target = 3.0f;
constexpr float w_k = 0.6f;
constexpr float f_ref = 960.0f;
constexpr float ref_rows = 1440.0f;
constexpr float w_cap_m = 20.0f;
constexpr float heavy_k = 0.91f; // the heavy class (hull-impact-classes rule 5/7: the heavy damage band)
// Section 2.2, per style: the rest length in w (tracer and slug x s_v = 0.75 + 0.5 min(v / sv_ref_mps, 1); the orb's
// diameter; the wave's thickness), the tail T = min(v tail_tau, tail_max_w w) with its root width (w) and gain, the halo
// sigma (w; the orb's of its own diameter), the core's whiteness h (heavy 1).
constexpr float rest_w[4] = {12.f, 4.f, 1.6f, 0.5f};
constexpr float sv_ref_mps = 2000.f;
constexpr float tail_tau_ms[5] = {15.f, 30.f, 10.f, 0.f, 40.f};
constexpr float tail_max_w[5] = {8.f, 12.f, 4.f, 0.f, 20.f};
constexpr float tail_root_w[5] = {0.6f, 0.35f, 0.8f, 0.f, 0.8f};
constexpr float tail_gain[5] = {0.5f, 0.6f, 0.5f, 0.f, 0.6f};
constexpr float core_r = 0.4f, core_r_heavy = 0.55f;   // the core's radius in rho (body half widths)
constexpr float halo_sigma_w[5] = {0.8f, 0.7f, 0.9f, 0.6f, 1.0f};
constexpr float e_body = 1.2f;
constexpr float e_halo = 0.6f, e_halo_heavy = 0.8f;
constexpr float h[4] = {0.75f, 0.85f, 0.7f, 0.5f};
constexpr float head_gain = 0.5f;     // the ball head: the core x (1 + head_gain (1 - s)) over its front 1 w
constexpr float head_ball_w = 0.6f;   // heavy: a disc of this radius (w) at core_e on the head
constexpr float tip_w = 1.0f;         // the tracer's pointed tip and the head's span, w
constexpr float slug_tail_white = 0.6f, slug_tail_end = 0.4f; // the slug tail: white -> t (at 0.6) -> 0.4 t
// Section 2.3, the streak: at most (1 + streak_c) rest lengths, energy kept (the stretch's (1 - u)^2 fall, the whole bolt
// x g_s = L / (L + e / 3)), the core held at core_min.
constexpr float streak_c[4] = {1.f, 1.f, 0.5f, 0.f};
constexpr float streak_c_simple = 0.5f;
constexpr float core_min = 1.2f;
// Section 2.5, expiry: x saturate((t_exp - now) / max(end_ms, 3 steps)).
constexpr unsigned end_ms = 60;
// The orb (2.2): the boil (two octaves of value noise at orb_freq cells per orb diameter, advected at orb_boil_hz game
// time; body x (0.75 + 0.5 n)), the silhouette lobes (hull-burst 2.4), the halo stretched along the travel, the wisp.
constexpr float orb_freq[2] = {3.0f, 6.4f};
constexpr float orb_boil_hz = 3.0f;
constexpr float orb_lobes = 5.f, orb_lobe_amp = 0.12f, orb_lobe_turns = 0.5f;
constexpr float orb_stretch = 1.3f;
constexpr float orb_h_lo = 0.5f, orb_h_hi = 0.9f; // the centre's whiteness breathing with the noise
// The wave (2.2): a crescent of chord S = wave_span_w w (col 18 once its unit is pinned), sagitta wave_sagitta S bowing
// forward, thickness rest_w[3] w, a sheet behind the arc at wave_sheet_e; foreshortened (c < wave_ring_c) a ring.
constexpr float wave_span_w = 6.f, wave_sagitta = 0.25f, wave_sheet_e = 0.15f, wave_ring_c = 0.4f;
// Section 2.7, the trim rule: a stream ratio above trim_rs cuts the core to trim_rs v / f - T / 2.
constexpr float trim_rs = 0.8f;
// Section 2.1, the rim: a multiply band at rim_band_w (w from the axis) darkening by rim_dark, drawn only where the band
// is at least rim_min_px wide; rim false keeps it off (the kill step, or a failed bolt_rim_survives_resolve).
constexpr bool rim = true;
constexpr float rim_dark = 0.4f;
constexpr float rim_band_w[2] = {1.2f, 1.8f};
constexpr float rim_min_px = 3.0f;
// Section 2.6, the floors and ceilings (render px at ref_rows; body and halo floors not row-scaled).
constexpr float halo_px_max = 40.0f;
constexpr float floor_core_px = 2.0f, floor_body_px = 3.0f, floor_halo_px = 1.5f, floor_len_px = 24.0f;
// Section 2.4's end-on footprint (lit diameter at 10 % of the peak >= 7 px at 1440 rows): with a 3 px body and a halo of
// sigma 1.5 px at E_h 0.6 the 10 % level (about 0.66 of a 6.6 peak) is never reached outside the body, so an end-on
// line's body floor rises to floor_endon_px x rows / 1440 while its projected axis is shorter than that, falling to the
// side-on floor linearly with the axis (fixture bolt_endon_disc, 2026-11-07). A size floor: radiance unchanged.
constexpr float floor_endon_px = 9.0f;
constexpr float own_body_max_px = 16.0f;
// Another ship's bolt passing the eye: its back end, clipped to the eye plane, never wider than its head or back_w_own_x
// x the own ceiling, whichever is wider (one frame near the eye would otherwise draw a frame-filling quad; review
// 2026-11-07 note 15).
constexpr float back_w_own_x = 4.0f;
constexpr bool cockpit_native = false;
// The far flag of the late bullet draw (bolts-through-taa.md B'): oC2 = far_flag_k x max(rgb).
constexpr float far_flag_k = 32.0f;
// Capacities (weapon-look 2.11): the tap list, the capsule quads of a frame (the bolts and the muzzles' elements).
constexpr unsigned list_capacity = 512;
constexpr unsigned capsule_quads = 1024;
// The tap past list_capacity (O(1) a call): each overflowing call examines the next far_window records from the list's
// ring cursor and keeps the farthest non-own one as the replacement candidate; the object table's probes stop after
// key_probes (a bullet whose key is not found within them is recorded without one).
constexpr unsigned far_window = 8, key_probes = 64;
// The parity rule (weapon-look 3.3, unknown 14): instances matched within parity_tol_px + the instance's own on-screen
// radius, among at most parity_window instances of the x-sorted list around the record; the row bodies learned from the
// first parity_learn records of every frame.
constexpr float parity_tol_px = 6.0f;
constexpr unsigned parity_window = 48, parity_learn = 32, parity_instances = 256;
// A beam object (flags 0x2 / 0x4) the tap sees: step 5 owns its look; until then the in-place draw keeps it visible as a
// plain line capsule from its node along its z row for ext+0x78 (engine units, unknown 3) at beam_w_m, because every
// bullet draw of the frame is suppressed.
constexpr float beam_w_m = 1.2f;
} // namespace bolt
// The muzzle flash (bolt-muzzle-look.md section 3): per class 0 energy (default, ion), 1 ballistic (ammunition 0x40), 2
// plasma (orb / wave body), 3 heavy (k >= bolt::heavy_k), 4 flak (0x100 / 0x1000); sizes in the weapon's w, capped by the
// shooter's radius R_s (obj+0xa4 / 500): core and star <= cap_core_rs R_s, flame and vents <= cap_flame_rs R_s; life in
// game ms (max(life, 3 steps), +-stagger / 2); re-lit by reset (one record per (ship, gun slot)).
namespace muzzle {
enum Mode : std::uint8_t { full = 0, simple = 1 };
constexpr Mode mode = full;
enum Class : std::uint8_t { energy = 0, ballistic = 1, plasma = 2, heavy = 3, flak = 4, class_count = 5 };
constexpr float class_disc[5][3] = {{0.6f, 0.7f, 4.f}, {0.5f, 0.7f, 4.f}, {0.8f, 0.6f, 4.f}, {0.8f, 1.0f, 4.f},
                                    {0.5f, 0.5f, 4.f}}; // diameter (w), whiteness h, radiance
constexpr float disc_halo_w = 0.4f, disc_halo_e = 0.6f;
constexpr float class_flame[5][3] = {{2.0f, 0.6f, 1.2f}, {1.2f, 0.5f, 1.2f}, {0.f, 0.f, 0.f}, {3.0f, 0.8f, 1.2f},
                                     {0.f, 0.f, 0.f}}; // cone length (w), base (w), radiance x (1 - s)^1.5
constexpr float class_star[5] = {0.f, 1.2f, 0.f, 0.f, 0.8f}; // the star's arm scale (w)
constexpr float star_arms[4] = {1.0f, 0.55f, 0.8f, 0.45f};
constexpr float star_width_w = 0.12f, star_e = 2.0f;
constexpr float bloom_w[2] = {0.5f, 1.0f}; // plasma: the noise bloom's radius over its life (w)
constexpr float bloom_e = 1.2f;
constexpr float vent_w = 1.2f, vent_ms = 150.f, vent_e = 1.2f; // heavy: two jets across the gun axis
constexpr float afterglow_ms = 250.f, afterglow_e = 1.2f;       // heavy: the disc's afterglow
constexpr float rise_ms = 40.f;                                  // heavy: the cone's rise
constexpr float class_smoke[5][5] = {{0.f, 0.f, 0.f, 0.f, 0.f}, {0.6f, 1.2f, 0.3f, 400.f, 0.5f}, {0.f, 0.f, 0.f, 0.f, 0.f},
                                     {0.8f, 1.6f, 0.4f, 600.f, 0.5f}, {0.6f, 1.2f, 0.3f, 400.f, 0.f}};
                                     // r0, r1 (w), strength s0, life ms, drift along the gun axis (w)
constexpr float class_glow[5][2] = {{3.0f, 0.6f}, {2.5f, 0.6f}, {3.5f, 0.8f}, {4.0f, 1.0f}, {2.0f, 0.5f}}; // R (w), E
constexpr float class_life[5] = {120.f, 60.f, 180.f, 80.f, 80.f};
constexpr float cap_core_rs = 0.25f, cap_flame_rs = 1.0f;
// The view rule (2026-11-07, the chase frames): a gun axis within 30 degrees of the eye ray (|cos| >= endon_cos) collapses
// the cone into its disc, so the cone is replaced by the hull burst's four-arm star (the sprite batch's star kind 3, the
// unequal arms of hull::star_arm_len) of the cone's length about the disc, at star_e, for the cone's life.
constexpr float endon_cos = 0.8660254f;
constexpr float breath = 0.25f;
constexpr float cluster_n0 = 2.5f;
constexpr unsigned record_capacity = 128; // live (ship, slot) records (weapon-look 2.11's muzzle cap)
} // namespace muzzle
} // namespace x3m::weapon_effects::core
