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
// while the stage owned the previous frame). User decision 2026-11-06: on a shield hit the weapon's native impact stays
// visible and the patch is drawn over it, so a stage record is normally forwarded too (suppress_native_shield_hit).
// The former flag_owner_retired (set only on a record about to be released) gave its bit to flag_stage.
enum EventFlag : std::uint8_t { flag_absorbed = 1, flag_forwarded = 2, flag_nested = 4, flag_stage = 8,
                                flag_override_applied = 16, flag_calibre_override = 32, flag_logged = 64,
                                flag_shield_read = 128 };
// Whether a stage-owned shield hit also skips the engine's spawn. false (user decision 2026-11-06): the native impact
// sprite is the weapon's energy arriving and stays; the patch is the shield's response, drawn in addition. Step 2 sets
// it once our own weapon impact exists (HookState::suppress_native carries it, the hook fixture drives both paths).
constexpr bool suppress_native_shield_hit = false;
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
    std::uint8_t slot, calibre_q;            // gun slot (muzzle, later steps); calibre override x 255 (flag_calibre_override)
    std::uint32_t tint_rgb, alt_rgb;         // the classifier's colour and the other candidate (light <-> texture)
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
static_assert(sizeof(Event) == 64, "the event record is 64 bytes (16 KB ring)");

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
constexpr std::uint32_t obj_dying = 0x8000000u;    // obj+0x40 bit set once by the kill routine 0x0044abd0
constexpr std::uint32_t obj_shielded = 0x100000u;  // obj+0x40 bit 20: docks, factories, ships at creation
constexpr std::uint16_t class_bullet = 0, class_shielded_a = 1, class_dock = 5, class_factory = 6, class_ship = 7,
                        class_missile = 10, class_shielded_b = 0x12;
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
                             row_texture = 16 };
struct Table {
    std::uint32_t rows = 0;           // TBullets rows of the generating game tree, 0 = no table
    std::uint32_t missile_rows = 0;
    std::uint8_t bits[table_capacity]{};
    std::uint8_t shape[table_capacity]{};     // from the elongation (or the override's style)
    std::uint8_t calibre_q[table_capacity]{}; // override x 255
    std::uint32_t texture_rgb[table_capacity]{};
    std::uint32_t override_rgb[table_capacity]{};
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
constexpr unsigned ring_capacity = 256;
struct Ring {
    Event slots[ring_capacity];
    std::uint16_t free_list[ring_capacity];
    unsigned free_count = 0;
    std::uint32_t next_sequence = 1;
    std::uint32_t overflow = 0, retired = 0;
};
inline void ring_clear(Ring& r) noexcept {
    for (unsigned i = 0; i < ring_capacity; ++i) {
        r.slots[i].sequence = 0;
        r.free_list[i] = std::uint16_t(ring_capacity - 1 - i);
    }
    r.free_count = ring_capacity;
}
inline void ring_release(Ring& r, unsigned i) noexcept {
    if (i >= ring_capacity || !r.slots[i].sequence) return;
    r.slots[i].sequence = 0;
    r.free_list[r.free_count++] = std::uint16_t(i);
}
// Overflow order: the oldest record the stage does not draw, hull_hit or muzzle record first, else the oldest of all (only when full).
inline unsigned ring_victim(const Ring& r) noexcept {
    unsigned best = ring_capacity, any = ring_capacity;
    std::uint32_t best_seq = 0xffffffffu, any_seq = 0xffffffffu;
    for (unsigned i = 0; i < ring_capacity; ++i) {
        const Event& e = r.slots[i];
        if (!e.sequence) continue;
        if (e.sequence < any_seq) {
            any_seq = e.sequence;
            any = i;
        }
        const bool cheap = !(e.flags & flag_stage) || e.role == role_hull_hit || e.role == role_muzzle;
        if (cheap && e.sequence < best_seq) {
            best_seq = e.sequence;
            best = i;
        }
    }
    return best < ring_capacity ? best : any;
}
// A slot for a new event (sequence assigned); never fails on a cleared ring.
inline Event* ring_acquire(Ring& r) noexcept {
    unsigned i;
    if (r.free_count)
        i = r.free_list[--r.free_count];
    else {
        i = ring_victim(r);
        if (i >= ring_capacity) return nullptr;
        ++r.overflow;
    }
    Event* e = &r.slots[i];
    e->sequence = r.next_sequence++;
    if (!r.next_sequence) r.next_sequence = 1;
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
inline unsigned ring_retire_owner(Ring& r, std::uint32_t owner) noexcept {
    unsigned n = 0;
    for (unsigned i = 0; i < ring_capacity; ++i)
        if (r.slots[i].sequence && r.slots[i].owner == owner) {
            ring_release(r, i);
            ++n;
        }
    r.retired += n;
    return n;
}

// ---------------------------------------------------------------------------------------------- the handler (2.2)
enum Mode : std::uint8_t { mode_native = 0, mode_observe = 1, mode_shields = 2 };
inline const char* mode_name(unsigned m) noexcept {
    return m == mode_observe ? "observe" : m == mode_shields ? "shields" : "native";
}
// X3M_WEAPON_EFFECTS: exactly "native", "observe" or "shields" (lower case); unset or empty = native (no family is
// accepted yet, note 2.6); "hull", "explosions", "bolts", "beams" are later steps and refused here like any other word.
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, Mode* out) noexcept {
    static const char* const words[3] = {"native", "observe", "shields"};
    if (!text) return false;
    for (unsigned w = 0; w < 3; ++w) {
        std::size_t len = 0;
        while (words[w][len]) ++len;
        if (n != len) continue;
        bool same = true;
        for (std::size_t i = 0; i < n && same; ++i) same = text[i] == Char(words[w][i]);
        if (same) {
            *out = Mode(w);
            return true;
        }
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
    std::uint32_t shield_read = 0, shield_unread = 0; // the hit object's shield list walked to its sentinel, or refused
};
struct HookState {
    std::uint8_t mode = mode_native;   // what initialize() installed (native: the stubs are not live)
    std::uint8_t armed = 0;            // families whose records the stage draws (bit 0: shields)
    volatile std::uint8_t owned_next = 0; // Present's latch: the stage owned the last frame
    std::uint8_t suppress_native = suppress_native_shield_hit; // a stage-owned shield hit also skips the native spawn
    Globals globals{};
    const Table* table = nullptr;      // the loaded table (null: classifier without table lookups)
    std::uint16_t lifetime_ms = 0;     // the records' game-time life (install: patch_lifetime_ms)
    Ring ring{};
    PosCheck check{};
    Counters counters{};
};
constexpr std::uint8_t family_shields = 1;

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
    std::uint8_t style = shape_slug, table = table_none, tint_source = tint_white, eflags = 0, calibre_q = 0;
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
        }
    } else {
        table = table_missiles;
        style = shape_missile_hit;
        tint = missile_tint_rgb;
        tint_source = tint_missile;
    }
    const std::uint8_t role = role_from(absorbed, pclass, flags);
    // The stage draws a shield hit while it owned the previous frame. The engine's spawn still runs unless
    // suppress_native is set (default off, user decision 2026-11-06: the native impact plus the patch). A dying hit
    // object is never the stage's: the stage retires a dying owner's records unseen, so it stays native only.
    const bool stage =
        s.mode == mode_shields && s.owned_next && (s.armed & family_shields) && role == role_shield_hit && !hit_dying;
    const bool skip = stage && s.suppress_native;
    // The hit object's shield fraction and patch tier (5.1.1), for every recorded hit (the observe flight compares
    // `absorbed` with the shield state).
    const std::uint16_t hit_class = victim_is_projectile ? sc : vc;
    const std::int32_t hit_radius = std::int32_t(rd32(hit + layout::obj_radius));
    std::uint16_t shield_q = 0;
    const bool shield_ok = read_shields(hit, hit_class, s.globals, &shield_q);
    std::uint8_t patch_tier = patch_fighter, tier_source = tier_by_radius;
    read_tier(hit, hit_class, hit_radius, s.globals, &patch_tier, &tier_source);
    Event* e = ring_acquire(s.ring);
    if (!e) return 0;
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
    e->lifetime_ms = s.lifetime_ms;
    e->slot = 0;
    e->calibre_q = calibre_q;
    e->tint_rgb = tint;
    e->alt_rgb = alt;
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
inline bool expired(std::uint32_t now, const Event& e) noexcept {
    const std::uint32_t a = age_ms(now, e.t0_game_ms);
    return a > e.lifetime_ms; // a wrapped (future) t0 is a huge age
}
// The patch look (docs/architecture/weapon-effects.md 5.1.1), the lab's controls (tools/effects/
// generate_weapon_effects_lab.py injects every value below by name). Hexagon cells of cell_m (centre to centre) fixed in
// world units per PatchTier on a stable grid in the owner's frame (six anchor regions, weapon_effects_stage.h); a hit
// lights the cell containing it and its rings neighbour rings (1 + 3 rings (rings + 1) cells), each cell when the ring
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
constexpr float cell_m[patch_tier_count] = {1.5f, 3.0f, 8.0f};     // fighter, corvette, capital/station (metres)
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
constexpr float soft = 0.5f;            // the soft fade against the lane, a multiple of the owner's mean axis
// The records' game-time life: the ring reaches the rim, then the slowest cell fades out.
constexpr unsigned patch_lifetime_ms = unsigned(1000.f / ripple_speed + float(fade_ms) * (1.f + 0.5f * stagger)) + 1;
static_assert(patch_lifetime_ms > 0 && patch_lifetime_ms < 65535, "the record's lifetime is a u16 of game ms");
// Engine integers to render (view) units: the gameplay context scale (own_sun_core.h context_scale).
constexpr float world_scale = 0.01f;
constexpr unsigned max_owners = 16;   // owners drawn per frame
constexpr unsigned max_patches = 32;  // patch draws per frame (one per owner and anchor region)
constexpr unsigned max_decals = 16;   // flat decals per frame (hits on an owner without a usable ellipsoid)
constexpr unsigned hit_slots = 4;     // ring and core hits per draw (the region's newest hits still in their ring)
constexpr unsigned owner_hits = 64;   // live hits per owner feeding its cell map (newest first)
constexpr unsigned cell_map_capacity = 128; // lit cells per draw (one owner and anchor region; a region spills into more)
constexpr unsigned disarm_frames = 64;

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
inline void event_tint(const Event& e, float mix, float out[3]) noexcept {
    tint_linear(e.tint_rgb, out);
    if (!(e.alt_rgb & rgb_present) || !(mix > 0.f)) return;
    float alt[3];
    tint_linear(e.alt_rgb, alt);
    const float t = mix > 1.f ? 1.f : mix;
    for (unsigned i = 0; i < 3; ++i) out[i] += (alt[i] - out[i]) * t;
}
} // namespace x3m::weapon_effects::core
