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
// Step 2 (docs/archive/effects-look-2026-11-08/weapon-look.md 3.1): in shields,hull mode a stage-owned hull hit (absorbed == 0, the
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
// The edition (docs/reverse-engineering/fl/weapon-effect-spawn.md sections 3 and 5): X3FL.exe keeps every offset below
// but the game clock (cfg +0x708) and the node body id (+0x144, so a node read reaches +0x148); its globals are
// base-relative and bound at install (fl_globals).
#if X3M_EDITION_FL
constexpr bool edition_fl = true;
#else
constexpr bool edition_fl = false;
#endif
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
constexpr unsigned cfg_game_ms = edition_fl ? 0x708 : 0x718;
// The ship's hull walk (render-node-bounds.md 6.1; ship_avoidance_walk_core.h hull_children): node links, placement,
// scales, flags and body id; the body cache's hash, its model's LOD records and their extents.
// AP VA of the body cache slot; FL: 0 here, the bound slot is Globals::body_cache_slot (base + 0x1aa100).
constexpr std::uintptr_t body_cache_root = edition_fl ? 0 : 0x00608518;
constexpr unsigned body_cache_hash = 0x14, node_children = 0x0c, node_sentinel = 0x10, node_offset = 0x30, node_basis = 0x40,
                   node_scale = 0x70, node_axis_scale = 0x80, node_flags = 0x12c;
constexpr unsigned node_body = edition_fl ? 0x144 : 0x140, node_walk_bytes = node_body + 4;
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
// The Effects row table and its element records (spawn note 10.1 / 10.2) served the clone row R' of the removed death
// look (docs/archive/effects-look-2026-11-08/); their anchors stay in the reverse-engineering note.
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
    std::uintptr_t body_cache_slot = layout::body_cache_root; // the ship bubble's hull walk (the stage's Tuning on FL)
};
static_assert(Globals{}.dock_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_dock &&
                  Globals{}.factory_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_factory &&
                  Globals{}.asteroid_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_asteroid &&
                  Globals{}.ship_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_ship &&
                  Globals{}.missile_table_slot == Globals{}.bullet_table_slot + 4 * layout::class_missile &&
                  Globals{}.asteroid_count_slot == Globals{}.bullet_count_slot + 4 * layout::class_asteroid &&
                  Globals{}.dock_count_slot == Globals{}.bullet_count_slot + 4 * layout::class_dock,
              "the type tables are indexed by class (0x00606fb8 + 4 class, counts 0x00607038 + 4 class)");
// FL (fl/weapon-effect-spawn.md sections 3 and 5): the same slots at module base + RVA. The count array sits below the
// table array (count = table - 0x98; AP's count = table + 0x80 is the edition's parameter, not a rule).
namespace fl_slots {
constexpr std::uint32_t cfg = 0x1a83b0, tables = 0x1a83c8, counts = 0x1a8330, body_cache = 0x1aa100;
static_assert(tables - counts == 0x98 && tables + 4 * 9 == 0x1a83ec && counts + 4 * 9 == 0x1a8354 &&
                  tables + 4 * 7 == 0x1a83e4 && counts + 4 * 7 == 0x1a834c,
              "the note's TShields 0x1a83ec / 0x1a8354 and TShips 0x1a83e4 / 0x1a834c: table + 4 class, count + 4 class");
}
inline Globals fl_globals(std::uintptr_t base) noexcept {
    const auto t = [base](unsigned c) { return base + fl_slots::tables + 4u * c; };
    const auto n = [base](unsigned c) { return base + fl_slots::counts + 4u * c; };
    Globals g;
    g.cfg_slot = base + fl_slots::cfg;
    g.bullet_table_slot = t(0);
    g.bullet_count_slot = n(0);
    g.missile_table_slot = t(10);
    g.missile_count_slot = n(10);
    g.ship_table_slot = t(7);
    g.ship_count_slot = n(7);
    g.shield_table_slot = t(9);
    g.shield_count_slot = n(9);
    g.dock_table_slot = t(5);
    g.dock_count_slot = n(5);
    g.factory_table_slot = t(6);
    g.factory_count_slot = n(6);
    g.asteroid_table_slot = t(17);
    g.asteroid_count_slot = n(17);
    g.body_cache_slot = base + fl_slots::body_cache;
    return g;
}
// Every slot 0 (FL before install: no engine read can land on an AP address).
inline Globals unbound_globals() noexcept {
    Globals g;
    g.cfg_slot = g.bullet_table_slot = g.bullet_count_slot = g.missile_table_slot = g.missile_count_slot = 0;
    g.ship_table_slot = g.ship_count_slot = g.shield_table_slot = g.shield_count_slot = 0;
    g.dock_table_slot = g.dock_count_slot = g.factory_table_slot = g.factory_count_slot = 0;
    g.asteroid_table_slot = g.asteroid_count_slot = g.body_cache_slot = 0;
    return g;
}

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
constexpr unsigned bullet_texture_capacity = 16; // tables.bullet_textures entries kept (the rest counted, dropped)
constexpr unsigned bullet_body_capacity = 64;    // tables.bullet_bodies entries kept (the rest counted, dropped)
struct Table {
    std::uint32_t rows = 0;           // TBullets rows of the generating game tree, 0 = no table
    std::uint32_t missile_rows = 0;
    std::uint8_t bits[table_capacity]{};
    std::uint8_t shape[table_capacity]{};     // from the elongation (or the override's style)
    std::uint8_t calibre_q[table_capacity]{}; // override x 255
    std::uint32_t texture_rgb[table_capacity]{};
    std::uint32_t override_rgb[table_capacity]{};
    std::uint8_t hull_class[table_capacity]{}; // the override's class pin (HullClass, with row_hull_class)
    // Step 4 (docs/archive/effects-look-2026-11-08/bolt-muzzle-look.md section 1): the weapon's mount tier (0 fighter, 1 corvette, 2 capital:
    // the smallest ship class that mounts it; a row the generator did not resolve keeps 0, the fighter fallback,
    // unknown 7) and its range in metres (speed x lifetime; 0: unknown, the stage takes the live record's).
    std::uint8_t mount_tier[table_capacity]{};
    std::uint16_t range_m[table_capacity]{};
    // The bolt hand pins (bolt-muzzle-look.md 11.5, weapon_overrides.json `bolt_*`): BoltPin bits, the body radiance E_b
    // x 100 and the orb diameter w_orb in w x 100 where their bit is set.
    std::uint8_t bolt_pin[table_capacity]{};
    std::uint8_t bolt_e_body_q[table_capacity]{};
    std::uint8_t bolt_w_orb_q[table_capacity]{};
    // The projectile stage's row identity (weapon_projectile_family.h): FNV-1a 32 of the entry's "id" and "body"
    // strings (0: absent or longer than 63 characters), so an enabled TBullets id finds its row and the rows sharing
    // its body without keeping the strings.
    std::uint32_t id_hash[table_capacity]{};
    std::uint32_t body_hash[table_capacity]{};
    // The bullet re-shade's per-texture luminance normaliser (tables.bullet_textures; bullet_reshade_core.h): per
    // texture frame a row draws, the byte count and FNV-1a 64 of the first 4096 and of all bytes the engine hands
    // D3DXCreateTextureFromFileInMemoryEx, and the level-0 luminance's 99th percentile x 65535.
    std::uint32_t bullet_textures = 0;
    std::uint32_t bullet_texture_bytes[bullet_texture_capacity]{};
    std::uint64_t bullet_texture_head[bullet_texture_capacity]{};
    std::uint64_t bullet_texture_hash[bullet_texture_capacity]{};
    std::uint16_t bullet_texture_p99_q[bullet_texture_capacity]{};
    // The bullet re-shade's geometric size (tables.bullet_bodies; bullet_reshade_core.h layout_constants): per bullet body
    // the FNV-1a 32 of its "body" string (the rows' body_hash) and its UV layout as 16.16 words (LayoutWord order:
    // flags, head u, v, R, fin axis v, rim v, u0, u1).
    std::uint32_t bullet_bodies = 0;
    std::uint32_t bullet_body_hash[bullet_body_capacity]{};
    std::int32_t bullet_body_layout[bullet_body_capacity][8]{};
};
// FNV-1a 32 of a NUL-terminated string (the table's id and body hashes; constexpr for the enabled lists).
constexpr std::uint32_t name_hash(const char* s) noexcept {
    std::uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h ^ std::uint8_t(*s)) * 0x01000193u;
    return h ? h : 1u;
}
enum BoltPin : std::uint8_t { pin_halo_off = 1, pin_tint_texture = 2, pin_e_body = 4, pin_w_orb = 8 };

// The ship profiles (weapon_effects.json schema 3, `ship_profiles`, tools/effects/weapon_effects_table.py; docs/
// architecture/weapon-effects.md 5.1.1 "Placement", 2026-11-12): per TShips row (0-based: the object's subtype
// obj+0x4a) a hull profile along the model's z in scene units: z0, z1 and profile_slices slices, slice i spanning z0 +
// i (z1 - z0) / profile_slices, each its lateral centre and half-widths (cx, cy, hx, hy; the scene's part positions with
// the body points scaled by the LOD-0 value / 65536). The class the generator read (TShips col 52) is kept as its
// engine id (ship_class_id_of) and checked against the type record's +0xc8 word at the stage. One heap block at load
// (patch.cpp), read-only afterwards; nothing per hit.
constexpr unsigned profile_slices = 32, profile_capacity = 1024, profile_rows = 4096;
struct ShipProfile {
    std::int32_t row = -1;      // the TShips index
    std::int32_t class_id = -1; // the class's engine id (-1: a token outside the table, never matched)
    float z0 = 0.f, z1 = 0.f;   // scene units, z1 > z0
    float slice[profile_slices][4]{}; // cx, cy, hx, hy (scene units; hx, hy >= 0, 0 for an empty slice)
};
struct ShipProfiles {
    unsigned count = 0;
    std::uint16_t slot_of[profile_rows]; // row -> slot + 1 (0: none)
    ShipProfile entries[profile_capacity];
    void clear() noexcept {
        count = 0;
        std::memset(slot_of, 0, sizeof slot_of);
    }
    const ShipProfile* find(std::int32_t row) const noexcept {
        if (row < 0 || row >= std::int32_t(profile_rows) || !slot_of[row]) return nullptr;
        return &entries[slot_of[row] - 1u];
    }
};
// The engine id of a TShips col 52 class token (the text-id reader 0x004ea440's table 0x00552a28: AP's 35 names and
// FL's four more, weapon-effect-spawn.md 8.1): a name, a decimal number (taken literally, as the reader does) or the
// census's `class_id:N`; -1 for anything else.
inline std::int32_t ship_class_id_of(const char* s) noexcept {
    if (!s) return -1;
    const char* digits = std::strncmp(s, "class_id:", 9) == 0 ? s + 9 : s;
    if (*digits >= '0' && *digits <= '9') {
        std::int32_t v = 0;
        for (; *digits; ++digits) {
            if (*digits < '0' || *digits > '9' || v > 100000000) return -1;
            v = v * 10 + (*digits - '0');
        }
        return v;
    }
    struct Name {
        const char* name;
        std::int32_t id;
    };
    static const Name names[] = {
        {"OBJ_SHIP_M0", 2020}, {"OBJ_SHIP_M1", 2021}, {"OBJ_SHIP_M2", 2022}, {"OBJ_SHIP_M3", 2023},
        {"OBJ_SHIP_M4", 2024}, {"OBJ_SHIP_M5", 2025}, {"OBJ_SHIP_M6", 2026}, {"OBJ_SHIP_M7", 2027},
        {"OBJ_SHIP_M8", 2142}, {"OBJ_SHIP_TP", 2030}, {"OBJ_SHIP_TS", 2031}, {"OBJ_SHIP_TL", 2032},
        {"OBJ_SHIP_TM", 2141}, {"OBJ_SHIP_TS_PIRATE", 2035}, {"OBJ_SHIP_TL_PIRATE", 2036}, {"OBJ_SHIP_GONER", 2039},
        {"OBJ_SHIP_UFO", 2068}, {"OBJ_SHIP_ASTRONAUT", 2064}, {"OBJ_SHIP_KHAAKCLUSTER", 2131},
        {"OBJ_SATELLITE", 2065}, {"OBJ_SATELLITE2", 2080}, {"OBJ_BEACON", 2060}, {"OBJ_JUMPBEACON", 2062},
        {"OBJ_MINE", 2066}, {"OBJ_MINE2", 2143}, {"OBJ_MINE3", 2144}, {"OBJ_MINETR", 2145},
        {"OBJ_ORBITALLASER", 2011}, {"OBJ_ORBITALLASER2", 2149}, {"OBJ_FIGHTDRONE", 2067}, {"OBJ_FIGHTDRONE2", 2146},
        {"OBJ_FIGHTDRONEUSC", 2147}, {"OBJ_FREIGHTDRONE", 2092}, {"OBJ_RECONDRONE", 2150}, {"OBJ_SPACEFLY", 2070},
        {"OBJ_RELAYBEACON", 2063}, {"OBJ_SHIP_SPECIALDRONE", 2151}, {"OBJ_SHIP_DRONECARRIER", 2152},
        {"OBJ_SHIP_MISSILE_FRIGATE", 2153}};
    for (const Name& n : names)
        if (!std::strcmp(s, n.name)) return n.id;
    return -1;
}

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
    static constexpr unsigned capacity = 96; // drawn: >= max_owners (asserted)
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

// ---------------------------------------------------------------------------------------------- the death sites (3.2)
// The death handler's integer half (docs/architecture/weapon-effects.md 2.4): the death sites' tiers, the one decision
// "ours / native" per record (decide) and why (Cause). The death look that drew the stage's records, its clone row R'
// for the big tiers and its per-tier laws were removed on 2026-11-08 (docs/archive/effects-look-2026-11-08/index.md;
// the R' anchors stay in docs/reverse-engineering/weapon-effect-spawn.md section 10.5). No mode the setting reaches
// arms the death family; the hook fixture drives it through install_at.
namespace boom {
// The death tiers: fighter, corvette, capital (patch_tier_of_ship, the radius bands for an unknown id), station
// (classes 5 / 6), missile (class 10); asteroid (class 17) and other (any other class): always native.
enum Tier : std::uint8_t { fighter = 0, corvette = 1, capital = 2, station = 3, missile = 4, asteroid = 5, other = 6,
                           tier_count = 7 };
constexpr unsigned drawn_tiers = 5;
inline const char* tier_name(unsigned t) noexcept {
    static const char* const names[tier_count] = {"fighter", "corvette", "capital", "station", "missile", "asteroid", "other"};
    return t < tier_count ? names[t] : "?";
}
// The tiers forwarded native (a bit per Tier): corvette, capital and station (their clone row R' left with the look).
constexpr std::uint8_t tier_native = 14; // (1 << corvette) | (1 << capital) | (1 << station)
static_assert(tier_native == ((1u << corvette) | (1u << capital) | (1u << station)), "the big tiers forward native");
// The seam: whether a record of this tier is the stage's with its native spawn skipped (ours) or forwarded native. The
// one place the decision is taken.
enum Decision : std::uint8_t { decide_native = 0, decide_ours = 1 };
constexpr Decision decide(unsigned tier) noexcept {
    return tier >= drawn_tiers || (tier_native & (1u << tier)) ? decide_native : decide_ours;
}
inline const char* decision_name(unsigned d) noexcept {
    return d == decide_ours ? "ours" : "native";
}
// Why a death record is what it is (a death record's Event::tier; the weapon_death row's `cause=`): ours (the spawn
// skipped), or forwarded native because the tier is native or the frame was not the stage's (latch, programs, family).
enum Cause : std::uint8_t { cause_ours = 0, cause_tier_native = 1, cause_not_owned = 2, cause_count = 3 };
inline const char* cause_name(unsigned c) noexcept {
    static const char* const names[cause_count] = {"ours", "tier_native", "not_owned"};
    return c < cause_count ? names[c] : "?";
}
// A staged death record's game-time life per drawn tier: the removed look's slowest element (its smoke's end), frozen as
// integers (the handler holds no float).
constexpr std::uint16_t record_life_ms[drawn_tiers] = {3201, 4801, 7901, 12601, 1281};
} // namespace boom
// ---------------------------------------------------------------------------------------------- the handler (2.2)
// The modes the setting reaches (2026-11-08, after the look layer was scratched: docs/archive/effects-look-2026-11-08/):
// native, observe and shields. The family modes below install the bolt and death sites beside the impact and arm the
// hook-side staging of hull hits, bolts, muzzles and deaths; no stage draws them any more, so nothing latches them
// (HookState::hull_ready, boom_ready, BoltHookState::owned / muzzle_owned stay 0) and every such record is census-only,
// forwarded native. They stay as the hook contract the redo builds on and the hook fixture drives them through
// install_at; parse_mode never yields them.
// mode_shields_hull: the shield hit as in shields mode plus the hull hit (family_hull). stage_mode() is true for every
// mode from shields up (the ownership latch, the ring's scope and the stage). mode_shields_hull_bolts: the hull mode plus
// the bolts and the muzzle (family_bolts: the per-bullet tap 0x0045ac7d, the muzzle site 0x00446268 and the flight-effect
// skip 0x004146c0, weapon_effects_bolt.h); hull_mode() is true for both hull modes. mode_shields_hull_explosions /
// mode_shields_hull_bolts_explosions: the hull modes plus the death sites (family_boom: 0x0044ac64 / 0x0044acb2,
// boom::decide); explosions_mode() is true for both, hull_mode() and stage_mode() too.
// mode_reshade (FL, user decision 2026-11-09; docs/reverse-engineering/fl/bullet-draw-batch.md section 6): shields mode
// plus the bullet re-shade. The batch site 0x097af8 is claimed with a scope stub that forwards every call and only
// marks the submit's extent (x3m_bullet_scope); the draw hook binds our pixel program in place of the game's
// INSTANCE_BULLETS one inside it. The projectile stage, its batch skip and traversal drop, the object tap and the muzzle
// site are off: the engine draws its own bolts and flashes. stage_mode() is true (the shield stage's latch and ring).
// On AP the mode runs as shields (no batch site there).
enum Mode : std::uint8_t { mode_native = 0, mode_observe = 1, mode_shields = 2, mode_shields_hull = 3,
                           mode_shields_hull_bolts = 4, mode_shields_hull_explosions = 5,
                           mode_shields_hull_bolts_explosions = 6, mode_reshade = 7 };
inline const char* mode_name(unsigned m) noexcept {
    return m == mode_observe                         ? "observe"
           : m == mode_shields                       ? "shields"
           : m == mode_shields_hull                  ? "shields,hull"
           : m == mode_shields_hull_bolts            ? "shields,hull,bolts"
           : m == mode_shields_hull_explosions       ? "shields,hull,explosions"
           : m == mode_shields_hull_bolts_explosions ? "shields,hull,bolts,explosions"
           : m == mode_reshade                       ? "reshade"
                                                     : "native";
}
inline bool stage_mode(unsigned m) noexcept {
    return m >= mode_shields && m <= mode_reshade;
}
inline bool reshade_mode(unsigned m) noexcept {
    return m == mode_reshade;
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
// X3M_WEAPON_EFFECTS (lower case): one word, "native", "observe", "shields" or "reshade". The names of the scratched families,
// "hull", "bolts" and "explosions", are accepted beside "shields" and ignored (an old setting such as
// "shields,hull,bolts,explosions" runs as shields), each reported once in *ignored (bit 0 hull, 1 bolts, 2 explosions;
// the caller logs weapon_effects_ignored); tokens in any order, spaces around each ignored. Unset or empty = native
// (note 2.6). Any other token ("beams"), a family name without "shields" (also beside "reshade"), an empty or repeated
// token is refused.
constexpr unsigned ignored_name_count = 3;
inline const char* ignored_name(unsigned i) noexcept {
    static const char* const names[ignored_name_count] = {"hull", "bolts", "explosions"};
    return i < ignored_name_count ? names[i] : "?";
}
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, Mode* out, unsigned* ignored = nullptr) noexcept {
    static const char* const words[7] = {"native", "observe", "shields", "hull", "bolts", "explosions", "reshade"};
    if (!text || !n) return false;
    unsigned seen = 0, words_seen = 0, single = 4;
    std::size_t at = 0;
    for (;;) {
        std::size_t end = at;
        while (end < n && text[end] != Char(',')) ++end;
        std::size_t a = at, b = end;
        while (a < b && text[a] == Char(' ')) ++a;
        while (b > a && text[b - 1] == Char(' ')) --b;
        unsigned w = 0;
        for (; w < 7; ++w) {
            std::size_t len = 0;
            while (words[w][len]) ++len;
            if (b - a != len) continue;
            bool same = true;
            for (std::size_t i = 0; i < len && same; ++i) same = text[a + i] == Char(words[w][i]);
            if (same) break;
        }
        if (w >= 7 || (seen & (1u << w))) return false; // an unknown, empty or repeated token
        seen |= 1u << w;
        if (w < 3 || w == 6) {
            single = w == 6 ? unsigned(mode_reshade) : w;
            ++words_seen;
        }
        if (end >= n) break;
        at = end + 1;
    }
    const unsigned families = (seen >> 3) & 7u; // hull, bolts, explosions
    if (words_seen != 1 || (families && single != mode_shields)) return false;
    *out = Mode(single);
    if (ignored) *ignored = families;
    return true;
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
            } else if (texture && (t->bolt_pin[subtype] & pin_tint_texture)) { // texture set: the table is ok
                // the bolt hand pin's tint (bolt-muzzle-look.md 11.5): the texture mean even over a saturated light, so
                // the impact and the bolt agree
                tint = texture;
                alt = light;
                tint_source = tint_texture;
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
// programs), death_forward (0) to forward.
// Forwarded without a record: outside the explosions modes, a null object, a missing node (or node+0x1c 0) or class
// 0x1c (the callee returns 0 at once), a bullet area death (class 0: step 7), and the rule-(b) missile (class 10 with
// the area flag and a non-zero killer: the callee itself makes no explosion). Every other death is recorded: the
// stage's (flag_stage; its native spawn skipped) or census-only (flag_forwarded: the big tiers, the asteroid, any other
// class, a frame the stage did not own). The class is tested first at both sites (0x0044ac64 is taken for any object
// with the bullet flag, spawn note 4). The record retires the object's other live records first (its hits; a staged
// hull hit stays). Nested kills (an area blast's deaths inside one outer kill routine, spawn note 6) arrive as separate
// calls, each its own record; one in the same game millisecond as the previous death is marked flag_nested (census). A
// death record also carries the node's rows 0 and 2 (x 127: row 2 in dir, row 0 in alt_rgb332 / detail / calibre_q as
// int8), the type record's death row (the census cross-check) and its Cause in tier.
enum DeathResult : int { death_forward = 0, death_skip = 1 };
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
    if (table_slot) {
        const std::uintptr_t base = rd32(table_slot);
        const std::int32_t count = std::int32_t(rd32(count_slot));
        if (base && sub >= 0 && sub < count) {
            const std::uintptr_t rec = base + std::uintptr_t(sub) * layout::type_stride;
            row = std::uint16_t(rd32(rec + row_offset));
            if (cls == layout::class_ship) class_id = std::int32_t(rd32(rec + layout::ship_class_id));
        }
    }
    const boom::Decision decision = boom::decide(tier);
    const bool latched = s.owned_next && s.boom_ready && (s.armed & family_boom);
    const std::uint8_t cause = decision == boom::decide_native ? boom::cause_tier_native
                               : !latched                      ? boom::cause_not_owned
                                                               : boom::cause_ours;
    const bool ours = cause == boom::cause_ours;
    const std::uintptr_t cfg = rd32(s.globals.cfg_slot);
    const std::uint32_t now = cfg ? rd32(cfg + layout::cfg_game_ms) : 0;
    const bool nested = s.counters.death_records && now == s.death_ms;
    ring_retire_owner(s.ring, std::uint32_t(obj), true);
    Event* e = ring_acquire(s.ring);
    if (!e) return death_forward; // forwarded: nothing would draw the death
    e->role = role_explosion;
    e->style = tier;
    e->tier = cause;
    e->flags = std::uint8_t((ours ? flag_stage : flag_forwarded) |
                            (nested ? flag_nested : 0));
    e->table = std::uint8_t(cls < 0x100 ? cls : 0xff);
    e->tint_source = std::uint8_t(site);
    e->row = row;
    e->t0_game_ms = now;
    e->lifetime_ms = ours ? boom::record_life_ms[tier] : 0;
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
    return ours ? death_skip : death_forward;
}
// ---------------------------------------------------------------------------------------------- the FL entry ABI
// X3FL.exe's sites pass their first arguments in ECX/EDX and keep the impact total in EBP with no absorbed flag in
// memory (docs/reverse-engineering/fl/weapon-effect-spawn.md sections 2 and 4): the FL entry shims
// (weapon_effects_hook.cpp) rebuild the AP stub contract from the FL registers and frame with these two rules and hand
// it to the same handlers. Integer only, plain loads, no call.
//
// The absorbed flag (section 2.1; the damage routine 0x053070..0x0533b3). The absorbed path (0x053161..0x05319d) sets
// [esp+0x10] = T (0x05318d), clears the remaining shield-stage damage [esp+0x14] (0x053196), sets ESI = 1
// (`lea esi,[ebp+1]` after `xor ebp,ebp`, 0x05319a) and, unless the flag-0x20 bypass sends it to the hull stage
// (0x0531c5 -> 0x0531d6, where ESI becomes the hull argument), jumps to the total reload `mov ebp,[esp+0x10]` at
// 0x0532e3; for a victim with obj+0x40 & 2 the `cmovg` at 0x0532fd then clamps EBP to 50,000, and no ESI write follows.
// The hull path never runs that reload: it ends at 0x0532e7 with EBP = T + h (0x053264 reload, `add ebp,esi` 0x0532a9)
// or EBP = T with ESI <= 0 (0x05326a), and ESI = h, the last term written at 0x05323d / 0x05329f / 0x0532b1 / 0x0532db,
// which can be 1. So ESI = 1, a cleared remaining and EBP = the clamped T identify the absorbed path, except where the
// clamp hides h = 1: a piloted victim with T >= 50,000 whose hull stage ran with remaining 0. Remaining 0 on the hull
// path means the walk drained every shield to 0 (0x053155): a shield still holding energy sends the next step to the
// absorb branch (0x05311d `jg 0x053161`). That residual case therefore reads absorbed here and the impact handler's
// drained-shield rule (post-hit shields below drained_shield_q: here 0) records it as a hull hit; a genuinely absorbed
// hit leaves the absorbing shield above 0. A flag-0x20 hit through charged shields reads 0 here (AP: 1); role_from
// makes it a hull hit on either edition.
constexpr std::int32_t fl_piloted_clamp = 50000;
constexpr std::uint32_t fl_obj_piloted = 2;
inline std::uint32_t fl_absorbed(std::uint32_t esi, std::uint32_t ebp, std::uint32_t walk_total, std::uint32_t remaining,
                                 std::uint32_t victim_flags) noexcept {
    if (esi != 1 || remaining != 0) return 0;
    std::int32_t t = std::int32_t(walk_total);
    if ((victim_flags & fl_obj_piloted) && t > fl_piloted_clamp) t = fl_piloted_clamp; // the cmovg at 0x0532fd
    return std::int32_t(ebp) == t ? 1u : 0u;
}
// The FL death call serves both AP death sites (section 2.6: the area-bullet path joins the common tail). Its census tag
// (Event::tint_source, AP's site) is rebuilt from AP's own branch: 0 (the area site) for an object with
// obj+0x40 & 0x40000000 whose TBullets row (subtype bounded by the count) has flag 0x100, else 1 (the other site).
constexpr std::uint32_t obj_area_candidate = 0x40000000u, bullet_area_flag = 0x100u;
static inline __attribute__((always_inline)) unsigned fl_death_site(std::uintptr_t obj, const Globals& g) noexcept {
    if (!obj || !(rd32(obj + layout::obj_flags) & obj_area_candidate)) return 1;
    const std::uintptr_t base = rd32(g.bullet_table_slot);
    const std::uint32_t count = rd32(g.bullet_count_slot), sub = rd16(obj + layout::obj_subtype);
    if (!base || sub >= count) return 1;
    return (rd32(base + std::uintptr_t(sub) * layout::type_stride + layout::bullet_flags) & bullet_area_flag) ? 0u : 1u;
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
// The ship bubble (user decision after Run238 Flight E, 5.1.1 "Placement"): a fighter or corvette owner (shield_bubble)
// gets a smooth ellipsoid in its frame: centre the hull bounds' centre,
// half-extents the hull bounds' half-extents x bubble_scale (the hull_children walk of render-node-bounds.md 6.1,
// weapon_effects_bubble.h), each floored at bubble_floor of the largest and cut to bubble_scale x the object radius; a failed walk
// takes a sphere of the object radius about the root node (bubble_fallback=). Per pixel the ray meets it twice: the near side draws at full
// radiance, the far side (seen around the hull's outline, the hull lane hiding what lies behind it) at
// bubble_far_gain; both add where both draw. A failed walk is retried after bubble_retry_ms of game time.
constexpr float bubble_scale = 1.15f;
// The surface of a shield hit (FL 67 A, main session decision 2026-11-12): the ship bubble for a fighter or corvette; the
// hull decal for a capital-tier owner (patch_capital: M0/M1/M2/M7/TL by its TShips class, every dock and factory, an
// unknown class in the capital radius band) and for an unknown tier (the stage's tier_of takes it as a capital), as for
// stations. The box-fitted ellipsoid cannot fit a long capital hull with fins: the Argon Titan's stored extents are
// per-axis maxima (x 151k at one z-station, y 166k from the fins), its bubble stood about 174k x 194k x 608k and its side
// hits (|q| 0.22-0.52) moved a median 621 m outward along the radial map; the decal lies on the hull at the contact.
// Since 2026-11-12 (user decision on FL run 68: capitals get the bubble, the decal was "acceptable but not right") a ship
// whose TShips row has an offline profile (ShipProfiles, class confirmed) takes the lathe bubble of that profile
// (weapon_effects_lathe.h) whatever its tier, and no hull walk runs for it; shield_bubble decides only for a ship
// without one (shield_surface).
inline bool shield_bubble(std::uint8_t patch_tier, std::uint8_t tier_source) noexcept {
    return patch_tier < patch_capital && tier_source != tier_by_station;
}
enum ShieldSurface : std::uint8_t { surface_decal = 0, surface_ellipsoid = 1, surface_lathe = 2 };
inline ShieldSurface shield_surface(std::uint8_t patch_tier, std::uint8_t tier_source, bool profile) noexcept {
    return profile ? surface_lathe : shield_bubble(patch_tier, tier_source) ? surface_ellipsoid : surface_decal;
}
constexpr float bubble_far_gain = 0.6f;
constexpr float bubble_floor = 1.f / 16.f;
constexpr unsigned bubble_retry_ms = 2000;
// At most this many hull walk slices per stage call (review F4: a crowded sector's first frame; a slice is at most
// stage::hull_slice_nodes nodes and about stage::hull_slice_reads reads of one owner's walk); the owners past it draw on
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

static_assert(max_owners <= VisibleOwners::capacity, "every owner a stage frame can draw");
// The view volume's sphere about a shield stage owner (stage::owner_in_view): its object radius (obj+0xa4) x this, past
// the bubble (bubble_scale), so an owner just past the screen edge whose bubble still reaches into it counts as drawn.
constexpr float visible_radius_scale = 2.0f;
// A hull hit's record life (game ms) when the stage owns it: the removed hull look's slowest element, frozen as an integer
// (docs/archive/effects-look-2026-11-08/index.md; the handler reads it, never a float). Present keeps a staged record
// HookState::life_floor_ms longer.
constexpr unsigned hull_lifetime_ms = 3601;
static_assert(hull_lifetime_ms > 0 && hull_lifetime_ms < 65535, "the record's lifetime is a u16 of game ms");

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

// ---------------------------------------------------------------------------------------------- bolt hooks (step 4)
// The bolt hooks' capacities (weapon_effects_bolt.h): the per-bullet tap list and the muzzle records. The bolt and muzzle
// look these fed was removed on 2026-11-08 (docs/archive/effects-look-2026-11-08/index.md).
namespace bolt {
constexpr unsigned list_capacity = 512;
// The tap past list_capacity (O(1) a call): each overflowing call examines the next far_window records from the list's
// ring cursor and keeps the farthest non-own one as the replacement candidate; the object table's probes stop after
// key_probes (a bullet whose key is not found within them is recorded without one).
constexpr unsigned far_window = 8, key_probes = 64;
} // namespace bolt
namespace muzzle {
constexpr unsigned record_capacity = 128; // live (ship, slot) records
} // namespace muzzle
} // namespace x3m::weapon_effects::core
