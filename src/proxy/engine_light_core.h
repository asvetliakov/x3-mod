#pragma once
#include <cstddef>
#include <cstdint>
#include "sse_scalar.h"
#include "engine_effects_core.h"
#include "engine_plumes_core.h"

// Portable core of the engine light on the hull (docs/architecture/engine-light.md; gap 8 of
// docs/architecture/engine-exhaust-gap-analysis.md): the option, the per-ship light table built from the previous
// frame's glow-jet records (engine_effects_core.h Ring), the per-node table that carries each light into the model
// space of the ship's hull nodes drawn in that frame, and the per-draw constants of the pixel twin
// (renderer/linear_engine_light_inc.h, c52-c202: a light per nozzle plate). No Windows dependency: the host tests
// compile it. No x87: float
// work through SSE scalars (doubles where world coordinates are subtracted), no float or double returned by value.
//
// One frame of latency, compensated for the ship's motion: at the start of frame N the ring holds frame N-1's records
// and the draw log frame N-1's routed hull draws (node, its parent node+0x18, handle, world rows). build_ships picks
// per ship (the jet's parent: the ship's root node) the brightest main nozzle of the scene view; build_nodes expresses
// that light in the model space of every logged hull node of the ship (the node itself the root, or hanging directly
// under it), L_local = W_{N-1}^-1 (L_{N-1} - t_{N-1}). A routed hull draw of frame N with the same node and handle
// places it with its own current world rows, L_N = W_N L_local + t_N, so the light rides on the hull whatever the ship
// did between the frames; the camera position and forward axis come from the same draw's view-inverse rows, the space
// the pixel program's eye vector is in. The route logs only the draws of ships already in the frame's table (the log
// stays small), so a ship lights from the second frame its jets are recorded in and the light then follows the
// records with one frame of latency. A ship whose records stop while its hull is still drawn (a nozzle off screen) is
// held for engine_light_hold frames on its moving hull and fades out (hold_ships).
namespace x3m::engine_light::core {
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;

// --------------------------------------------------------------------------- option
// X3M_ENGINE_LIGHT=on|off (ini engine_light), read once at load; exact lowercase (anything else refused: on, status
// invalid_setting). Effective only with engine_effects = plumes.
enum class Mode : std::uint8_t { off = 0, on = 1 };
inline const char* mode_name(Mode m) noexcept {
    return m == Mode::on ? "on" : "off";
}
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, Mode* out) noexcept {
    if (n == 2 && text[0] == 'o' && text[1] == 'n') {
        *out = Mode::on;
        return true;
    }
    if (n == 3 && text[0] == 'o' && text[1] == 'f' && text[2] == 'f') {
        *out = Mode::off;
        return true;
    }
    return false;
}
constexpr Mode default_mode = Mode::on;
// X3M_ENGINE_LIGHT_HOLD (ini engine_light_hold), read once at load: how many frames a ship's light outlives its last
// main-jet record while the ship's hull is still drawn (hold_ships; engine-light.md "Hold"); one plain integer
// 0..hold_max, digits only, at most three (anything else refused: hold_default, status invalid_setting); 0 = no hold.
constexpr unsigned hold_default = 60, hold_max = 600;
template <class Char> inline bool parse_hold(const Char* text, std::size_t n, unsigned* out) noexcept {
    if (!text || !out || n < 1 || n > 3) return false;
    unsigned v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (text[i] < Char('0') || text[i] > Char('9')) return false;
        v = v * 10u + unsigned(text[i] - Char('0'));
    }
    if (v > hold_max) return false;
    *out = v;
    return true;
}
// The held light's scale at `age` frames without a record (1 .. window): 1 until the window's last third (F =
// ceil(window / 3) frames), then (window + 1 - age) / (F + 1), linear down to 1 / (F + 1) at age = window; the next
// frame drops the light (the ramp's zero). Window 60: 1 through age 40, 20/21 at 41 .. 1/21 at 60, gone at 61.
inline float hold_fade(unsigned age, unsigned window) noexcept {
    const unsigned last = (window + 2) / 3;
    if (age + last <= window) return 1.f;
    if (age > window) return 0.f;
    return float(int(window + 1 - age)) / float(int(last + 1));
}

// --------------------------------------------------------------------------- the law
// Placement: behind x value_eff along the plume axis from the nozzle (into the exhaust); reach: the radius of influence
// reach x value_eff (quadratic falloff saturate(1 - d^2 / R^2)^2, zero at R); colour: the record's mean tint x I(s) x
// the preset's scale x colour_scale, I(s) = lerp(core_low, core_high, s) (1.2 .. 4 at the default look), so a full
// throttle light at the default preset carries the tint at 1.0 and an idle one at 0.3. The pixel program caps the sum
// at 1 (EngineLightAbi: the lit plate stays below the plume's own radiance class).
constexpr float behind = .5f, reach = 3.f, colour_scale = .25f;
constexpr unsigned ship_capacity = 256;     // lights per frame (ships); beyond it the dimmest gives way (build_ships)
constexpr unsigned ship_slots = 512;        // open addressing, a power of two above twice the capacity
constexpr unsigned ship_block = 16, ship_blocks = ship_capacity / ship_block; // the full table's dimmest-entry blocks
constexpr unsigned log_capacity = 4096;     // routed hull draws logged per frame; further draws are counted dropped
constexpr unsigned node_capacity = 2048;    // hull nodes carrying a light; further nodes are counted dropped
constexpr unsigned node_slots = 4096;
// Nozzle plates (docs/architecture/engine-light.md "Nozzle plates"): up to plate_slots main nozzles per ship carry the
// light-map gain suppression of the twin and a light each (one light per plate, user decision 2026-10-08). Each plate
// is the point its nozzle's light sits at (behind x value_eff along the plume axis) with that nozzle's value_eff and
// colour; the twin's weight is the maximum over the ship's plates, and each hull pixel takes the light of the plate
// nearest in units of value_eff (the largest falloff), one falloff evaluation per pixel. Brightest first (I(s) x
// value_eff; ties: the lower node handle, then the earlier record); one plate per node handle; a smaller co-located
// layer of another record (engine_plumes merge_layers) is a plate at its natural value_eff (no floor), as the plume
// stage draws it. Plate 0 is the ship's brightest nozzle, the Light's own record.
// The cap covers every ship (engine-light.md "Plate cap"; verification/results/engine-light-plate-cap/nozzle_counts.py
// over the installed and the stock ship scenes: at most 66 main nozzles, the Boron Segaris and the stock Boron M7
// drone carrier; 19 installed ships above 16): 72 slots, the last tier's run (plate_runs), two registers per further
// plate between c55 and c197 (block_first). A ship beyond it still drops its dimmest nozzles (plates_dropped).
constexpr unsigned plate_slots = 72;
struct Plate {
    double position[3]{};           // world, the record's space (the nozzle's light point)
    float value = 0.f;              // the nozzle's value_eff (world units; the light's radius is reach x value)
    float brightness = 0.f;         // I(s) x value_eff: the order key
    float colour[3]{};              // the nozzle's light colour (linear, record_light)
    std::uint32_t handle = 0;
};
// One record's light, and the selection fields of a ship's entry (Light): kept apart from the plate list so a record
// costs its 64 bytes, not the entry's 3.5 KB (build_ships constructs one per record).
struct LightFields {
    std::uint32_t root = 0;         // the ship's root node (the jets' node+0x18)
    double position[3]{};           // world, the record's space (plate 0's light)
    float colour[3]{};              // linear
    float radius = 0.f;             // world units
    float brightness = 0.f;         // I(s) x value_eff: the selection key
    std::uint32_t handle = 0;       // the brightest jet's node handle (ties: the lower one wins): plate 0
    float s = 0.f, value = 0.f;     // the brightest jet's throttle and value_eff (the row's diagnostics)
    bool own = false;               // a record of the own ship fed it (never evicted by a full table)
};
// One logged draw a ship's world positions can be carried by (hold_ships): node, handle and that draw's world rows.
struct Anchor {
    std::uint32_t node = 0, handle = 0;
    float rows[12]{};
};
constexpr unsigned anchor_slots = 4;
struct Light : LightFields {
    unsigned plate_count = 0;
    // Hold (hold_ships): up to anchor_slots logged draws of the ship of the frame its world positions are in (the
    // root's own draw first when logged, then the first logged draws of nodes directly under the root; none: the entry
    // cannot be held), the brightness at its last record (the selection key `brightness` of a held entry is that times
    // its fade), the frames since the ship's last record (0: a record this frame) and the scale build_nodes applies to
    // its light and plates (1 unless held in the window's last third).
    Anchor anchors[anchor_slots];
    unsigned anchor_count = 0;
    float bound_brightness = 0.f;
    unsigned age = 0;
    float fade = 1.f;
    Plate plates[plate_slots];      // the ship's main nozzles, brightest first (add_plate)
};
struct ShipStats {
    // dropped: lights lost to the full table (a dimmer newcomer refused, or an entry evicted by a brighter or own one);
    // unfloored: main-jet records at their natural value as a smaller co-located layer (still a plate); plates_dropped:
    // main nozzles beyond the ship's plate_slots (the dimmest give way)
    unsigned records = 0, main = 0, rcs = 0, brake = 0, other_view = 0, invalid = 0, orphan = 0, dropped = 0;
    unsigned unfloored = 0, plates_dropped = 0;
    // held: entries re-entered by hold_ships without a record; hold_expired: entries whose window ran out;
    // hold_walked: entries not held because the node-sourced walk read the ship's whole list and found no live nozzle
    unsigned held = 0, hold_expired = 0, hold_walked = 0;
};
struct ShipTable {
    Light lights[ship_capacity];
    std::uint16_t slot[ship_slots]; // 0 empty, else index + 1
    unsigned count = 0;
    ShipStats stats{};
    // Full table only: per block of ship_block entries its evictable (non-own) entry of least brightness (-1: none).
    std::int16_t block_dimmest[ship_blocks];
    bool blocks_known = false;
    int dimmest = -1; // the minimum over the blocks, -1 until computed (dropped when a block changes)
    void clear() noexcept {
        count = 0;
        stats = {};
        blocks_known = false;
        dimmest = -1;
        for (unsigned i = 0; i < ship_slots; ++i) slot[i] = 0;
    }
};
inline unsigned hash_key(std::uint32_t a, std::uint32_t b, unsigned mask) noexcept {
    std::uint32_t x = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca77u;
    x ^= x >> 15;
    x *= 0x2c1b3c6du;
    x ^= x >> 12;
    return x & mask;
}
// The ship's light index, or -1. Bounded probe (the table is at most half full).
inline int find_ship(const ShipTable& t, std::uint32_t root) noexcept {
    if (!root) return -1;
    unsigned h = hash_key(root, 0, ship_slots - 1);
    for (unsigned probe = 0; probe < ship_slots; ++probe, h = (h + 1) & (ship_slots - 1)) {
        const unsigned s = t.slot[h];
        if (!s) return -1;
        if (t.lights[s - 1].root == root) return int(s - 1);
    }
    return -1;
}
// The slot holding the ship `root`, or -1.
inline int find_ship_slot(const ShipTable& t, std::uint32_t root) noexcept {
    if (!root) return -1;
    unsigned h = hash_key(root, 0, ship_slots - 1);
    for (unsigned probe = 0; probe < ship_slots; ++probe, h = (h + 1) & (ship_slots - 1)) {
        const unsigned s = t.slot[h];
        if (!s) return -1;
        if (t.lights[s - 1].root == root) return int(h);
    }
    return -1;
}
// Linear-probing removal without tombstones: the slot at `hole` is emptied and every later entry of its cluster whose
// home slot does not lie cyclically in (hole, j] moves back into the hole, so every probe still reaches its entry.
inline void erase_ship_slot(ShipTable& t, unsigned hole) noexcept {
    constexpr unsigned mask = ship_slots - 1;
    for (unsigned j = (hole + 1) & mask; t.slot[j]; j = (j + 1) & mask) {
        const unsigned home = hash_key(t.lights[t.slot[j] - 1].root, 0, mask);
        const bool stays = hole <= j ? (hole < home && home <= j) : (hole < home || home <= j);
        if (stays) continue;
        t.slot[hole] = t.slot[j];
        hole = j;
    }
    t.slot[hole] = 0;
}
// The full table's evictable entry of least brightness (ties: the later index), -1 when every entry is the own ship's.
// Kept per block of ship_block entries: a replacement or a brightened entry rescans its block only, so a newcomer costs
// ship_blocks + ship_block compares rather than ship_capacity, and a refused one reads the cached minimum (1,024
// records in ascending brightness, every newcomer a replacement: 23 us on the arm64 host, 124 us with a whole-table
// rescan; descending: 9 us; measured).
inline void rescan_ship_block(ShipTable& t, unsigned block) noexcept {
    int at = -1;
    for (unsigned i = block * ship_block; i < (block + 1) * ship_block; ++i)
        if (!t.lights[i].own && (at < 0 || !(t.lights[i].brightness > t.lights[at].brightness))) at = int(i);
    t.block_dimmest[block] = std::int16_t(at);
    t.dimmest = -1;
}
inline int dimmest_ship(ShipTable& t) noexcept {
    if (!t.blocks_known) {
        for (unsigned b = 0; b < ship_blocks; ++b) rescan_ship_block(t, b);
        t.blocks_known = true;
    }
    if (t.dimmest >= 0) return t.dimmest;
    int at = -1;
    for (unsigned b = 0; b < ship_blocks; ++b) {
        const int c = t.block_dimmest[b];
        if (c >= 0 && (at < 0 || !(t.lights[c].brightness > t.lights[at].brightness))) at = c;
    }
    t.dimmest = at;
    return at;
}
// A record's light (false: not a main jet with geometry, or non-finite). `radius` the ship's radius beside the record
// (Ring::parent_radius; 0 = none) for the plume floor's value_eff, `preset_scale` the stage's preset.
inline bool record_light(const ee::Record& r, const ee::Body* body, float radius, const ep::Look& look,
                         float preset_scale, LightFields* out) noexcept {
    if (r.flags & (ee::flag_steering | ee::flag_brake | ee::flag_rows_unknown)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!ee::finite_f(r.origin[i]) || !ee::finite_f(r.axis[i])) return false;
    if (!ee::finite_f(r.size) || !(r.size > 0.f) || !ee::finite_f(r.s) || !ee::finite_f(preset_scale) ||
        !(preset_scale > 0.f))
        return false;
    float value = r.size;
    ep::floored_value(look, r, radius, &value);
    if (!ee::finite_f(value) || !(value > 0.f)) return false;
    float s = r.s < 0.f ? 0.f : r.s > 1.f ? 1.f : r.s;
    const float intensity = look.core_low + (look.core_high - look.core_low) * s;
    float mean[3], peak[3];
    ep::record_tint(r, body, mean, peak);
    LightFields l{};
    const float offset = behind * value;
    for (unsigned i = 0; i < 3; ++i) {
        l.position[i] = double(r.origin[i]) + double(r.axis[i]) * double(offset);
        l.colour[i] = mean[i] * intensity * preset_scale * colour_scale;
        if (!ee::finite_f(l.colour[i]) || l.colour[i] < 0.f) return false;
    }
    l.radius = reach * value;
    l.brightness = intensity * value;
    l.handle = r.node_handle;
    l.s = s;
    l.value = value;
    if (!ee::finite_f(l.radius) || !(l.radius > 0.f) || !ee::finite_f(l.brightness)) return false;
    *out = l;
    return true;
}
// The record's plate into the ship's list (brightest first; ties: the lower handle, then the earlier record). A second
// record of a handle already listed keeps the brighter of the two; beyond plate_slots the dimmest gives way.
inline void add_plate(Light& ship, const LightFields& record, ShipStats& stats) noexcept {
    Plate p{};
    for (unsigned i = 0; i < 3; ++i) p.position[i] = record.position[i];
    p.value = record.value;
    p.brightness = record.brightness;
    for (unsigned i = 0; i < 3; ++i) p.colour[i] = record.colour[i];
    p.handle = record.handle;
    const auto before = [](const Plate& a, const Plate& b) {
        return a.brightness > b.brightness || (a.brightness == b.brightness && a.handle < b.handle);
    };
    unsigned n = ship.plate_count;
    for (unsigned i = 0; i < n; ++i)
        if (ship.plates[i].handle == p.handle) {
            if (!before(p, ship.plates[i])) return;
            for (unsigned j = i; j + 1 < n; ++j) ship.plates[j] = ship.plates[j + 1]; // re-inserted below
            --n;
            break;
        }
    unsigned at = n;
    while (at > 0 && before(p, ship.plates[at - 1])) --at;
    if (at >= plate_slots) {
        ++stats.plates_dropped;
        ship.plate_count = n;
        return;
    }
    if (n >= plate_slots) {
        ++stats.plates_dropped;
        n = plate_slots - 1;
    }
    for (unsigned j = n; j > at; --j) ship.plates[j] = ship.plates[j - 1];
    ship.plates[at] = p;
    ship.plate_count = n + 1;
}
// Ship lights from one frame's records: per root the brightest main nozzle (brightness I(s) x value_eff; ties: the
// lower node handle, then the earlier record), only records of the scene view (scene phase and the scene camera's
// handle, as the plume stage draws them; `camera`/`scene` null = every record) with a known parent. RCS (steering) and
// brake-pushed bodies never feed it. At most ship_capacity ships: a new ship beyond that replaces the dimmest entry
// (brightness I(s) x value_eff) when it is brighter, and always when it is the own ship's (`own`: Ring::own, null = no
// record is); own-ship entries are never the ones replaced. Every light lost to the cap counts dropped (dimmest_ship:
// the block minima). Every main nozzle of the scene view also feeds its ship's plates (add_plate); one that merge_layers
// marks as a smaller co-located layer of another record of the ship (`parents` as the plume stage passes them; records
// past ee::ring_capacity never are) takes no floor, its light and plate at its natural value, as the plume stage draws
// it; the Light's own fields (plate 0's) are chosen over all main records as before, and each plate carries its own
// nozzle's light (block_constants).
using BodyLookup = const ee::Body* (*)(int index);
inline void build_ships(const ee::Record* records, const std::uint32_t* parents, const float* radii,
                        const std::uint32_t* camera, const std::uint8_t* scene, std::uint32_t scene_handle,
                        unsigned count, BodyLookup body, const ep::Look& look, float preset_scale,
                        ShipTable* out, const std::uint8_t* own = nullptr) noexcept {
    out->clear();
    if (!records || !parents) return;
    std::uint8_t unfloor[ee::ring_capacity];
    const unsigned merging = count < ee::ring_capacity ? count : ee::ring_capacity;
    ep::merge_layers(records, merging, parents, unfloor);
    for (unsigned i = 0; i < count; ++i) {
        const ee::Record& r = records[i];
        ++out->stats.records;
        if (r.flags & ee::flag_steering) {
            ++out->stats.rcs;
            continue;
        }
        if (r.flags & ee::flag_brake) {
            ++out->stats.brake;
            continue;
        }
        if (camera && scene && (!scene[i] || camera[i] != scene_handle)) {
            ++out->stats.other_view;
            continue;
        }
        const std::uint32_t root = parents[i];
        if (!root) {
            ++out->stats.orphan;
            continue;
        }
        LightFields l;
        const ee::Body* b = body && r.body >= 0 ? body(r.body) : nullptr;
        const bool natural = i < merging && unfloor[i];
        if (!record_light(r, b, radii && !natural ? radii[i] : 0.f, look, preset_scale, &l)) {
            ++out->stats.invalid;
            continue;
        }
        ++out->stats.main;
        l.root = root;
        l.own = own && own[i];
        out->stats.unfloored += natural;
        const int found = find_ship(*out, root);
        if (found >= 0) {
            Light& have = out->lights[found];
            add_plate(have, l, out->stats);
            const bool was_own = have.own;
            bool changed = false;
            if (l.brightness > have.brightness || (l.brightness == have.brightness && l.handle < have.handle)) {
                // The selection fields only: the plate list stays.
                have.position[0] = l.position[0];
                have.position[1] = l.position[1];
                have.position[2] = l.position[2];
                for (unsigned k = 0; k < 3; ++k) have.colour[k] = l.colour[k];
                have.radius = l.radius;
                have.brightness = l.brightness;
                have.handle = l.handle;
                have.s = l.s;
                have.value = l.value;
                have.own = l.own;
                changed = true;
            }
            if (was_own || l.own) {
                changed = changed || !was_own;
                have.own = true;
            }
            if (changed && out->blocks_known) rescan_ship_block(*out, unsigned(found) / ship_block);
            continue;
        }
        unsigned index = out->count;
        if (out->count >= ship_capacity) {
            const int victim = dimmest_ship(*out);
            ++out->stats.dropped; // the newcomer or the entry it replaces
            if (victim < 0 || !(l.own || l.brightness > out->lights[victim].brightness)) continue;
            erase_ship_slot(*out, unsigned(find_ship_slot(*out, out->lights[victim].root)));
            index = unsigned(victim);
        } else
            ++out->count;
        unsigned h = hash_key(root, 0, ship_slots - 1);
        while (out->slot[h]) h = (h + 1) & (ship_slots - 1);
        static_cast<LightFields&>(out->lights[index]) = l; // the plate list stays as it is until plate_count
        out->lights[index].plate_count = 0;
        out->lights[index].anchor_count = 0; // hold_ships sets the anchors from the log
        out->lights[index].age = 0;
        out->lights[index].fade = 1.f;
        add_plate(out->lights[index], l, out->stats);
        out->slot[h] = std::uint16_t(index + 1);
        if (out->blocks_known) rescan_ship_block(*out, index / ship_block);
    }
}

// --------------------------------------------------------------------------- draw log and node table
// One routed hull draw of a frame: its scope node, the node's parent (node+0x18), handle and the world rows the vertex
// program reads (world_i = dot(rows[i*4..i*4+2], p) + rows[i*4+3]).
struct LoggedDraw {
    std::uint32_t node = 0, parent = 0, handle = 0;
    float world[12]{};
};
struct DrawLog {
    LoggedDraw draws[log_capacity];
    unsigned count = 0, dropped = 0;
    std::uint32_t last_node = 0, last_handle = 0; // consecutive draws of one node log once
    void clear() noexcept {
        count = dropped = 0;
        last_node = last_handle = 0;
    }
    void push(std::uint32_t node, std::uint32_t parent, std::uint32_t handle, const float world[12]) noexcept {
        if (!node || (node == last_node && handle == last_handle)) return;
        last_node = node;
        last_handle = handle;
        if (count >= log_capacity) {
            ++dropped;
            return;
        }
        LoggedDraw& d = draws[count++];
        d.node = node;
        d.parent = parent;
        d.handle = handle;
        for (unsigned i = 0; i < 12; ++i) d.world[i] = world[i];
    }
};
struct NodeLight {
    std::uint32_t node = 0, handle = 0;
    float local[3]{};   // the light in the node's model space
    float colour[3]{};
    float radius = 0.f;
    unsigned plate_count = 0;
    float plate[plate_slots][4]{}; // the ship's plates in the node's model space, value_eff in .w (world units)
    float plate_colour[plate_slots][3]{}; // each plate's light colour (plate 0's equals colour)
};
struct NodeStats {
    unsigned logged = 0, matched = 0, singular = 0, dropped = 0, log_dropped = 0;
};
struct NodeTable {
    NodeLight nodes[node_capacity];
    std::uint16_t slot[node_slots];
    unsigned count = 0;
    unsigned ships = 0; // ships with at least one node
    NodeStats stats{};
    void clear() noexcept {
        count = ships = 0;
        stats = {};
        for (unsigned i = 0; i < node_slots; ++i) slot[i] = 0;
    }
};
inline const NodeLight* find_node(const NodeTable& t, std::uint32_t node, std::uint32_t handle) noexcept {
    if (!node || !t.count) return nullptr;
    unsigned h = hash_key(node, handle, node_slots - 1);
    for (unsigned probe = 0; probe < node_slots; ++probe, h = (h + 1) & (node_slots - 1)) {
        const unsigned s = t.slot[h];
        if (!s) return nullptr;
        const NodeLight& n = t.nodes[s - 1];
        if (n.node == node && n.handle == handle) return &n;
    }
    return nullptr;
}
// The inverse of the world rows' 3x3 in double (adjugate over determinant); false when singular or not finite.
inline bool invert_rows(const float rows[12], double inverse[9]) noexcept {
    const double a[9] = {rows[0], rows[1], rows[2], rows[4], rows[5], rows[6], rows[8], rows[9], rows[10]};
    const double c[9] = {a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
                         a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
                         a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]};
    const double det = a[0] * c[0] + a[1] * c[3] + a[2] * c[6];
    const double m = scalar::abs(det);
    double scale = 0.;
    for (unsigned i = 0; i < 9; ++i) scale += scalar::abs(a[i]);
    if (!(m > 1e-12 * scale * scale * scale) || !(m < 1e300)) return false;
    const double r = 1. / det;
    for (unsigned i = 0; i < 9; ++i) inverse[i] = c[i] * r;
    for (unsigned i = 0; i < 9; ++i)
        if (!(inverse[i] == inverse[i]) || !(inverse[i] - inverse[i] == 0.)) return false;
    return true;
}
// Hold (engine-light.md "Hold"), after build_ships and before build_nodes at the frame boundary: `cur` this boundary's
// table (the previous frame's records), `prev` the table the previous frame drew with (its draws are the ones logged),
// `log` the previous frame's draw log, `window` engine_light_hold (0: nothing, the table as build_ships left it).
// - Anchors: every entry of cur, fresh or held, takes up to anchor_slots of the ship's logged draws as it is drawn now:
//   the root's own draw first when logged, then the first logged draws of nodes directly under the root (a node and
//   handle's first draw of the frame, as build_nodes places it); their rows are of the same frame as the entry's world
//   positions. The set follows the ship's current LOD each frame it is drawn.
// - Held: an entry of prev with anchors and without an entry in cur (no main-jet record of the ship that frame) is
//   re-entered in cur for age + 1 <= window frames while any of its anchors (same node and handle) is in the log: its
//   light and plate positions carried by the first such anchor whose earlier rows invert, from those rows to its current
//   ones, P' = W' W^-1 (P - t) + t' (the model space of the anchor: invert_rows), fade = hold_fade(age + 1), selection
//   key brightness x fade. A ship none of whose anchors is logged leaves at once; an expired one counts hold_expired. A
//   held entry keeps its own flag; a full table takes it in place of its dimmest evictable entry when it is the own
//   ship's or brighter after the fade (build_ships' rule), counted dropped either way. Colours, radius and values stay
//   at their bound strength; build_nodes applies the fade.
// - Walked (engine-nozzle-source.md section 7): a prev entry whose root is among `walked` (the roots whose child list
//   the node-sourced walk read to its end that frame, at most walked_count of them) is never held: the walk is
//   authoritative, no live nozzle means dark (hold_walked). The hold stays for roots the walk did not cover (the hull
//   not drawn in the scene view, a refused or cut walk, engine_nozzle_source = draw: walked empty).
// Cost: one pass over the log (at most four ship probes per draw, as build_nodes makes two, and up to anchor_slots
// compares per set), then per held ship one 3x3 inverse and product and nine multiply-adds per plate (positions in
// double) and a linear scan of `walked` per prev entry without a record; no allocation (6.5 KB of log indices on the
// stack).
inline void hold_ships(const ShipTable& prev, ShipTable* cur, const DrawLog& log, unsigned window,
                       const std::uint32_t* walked = nullptr, unsigned walked_count = 0) noexcept {
    static_assert(log_capacity <= 32767, "a log index fits the anchor arrays");
    if (!window) return;
    // Per cur entry and per prev entry the log indices of its anchor set as drawn now; per prev entry the log index of
    // each of its old anchors found again (-1: not drawn).
    std::int16_t drawn_cur[ship_capacity][anchor_slots], drawn_prev[ship_capacity][anchor_slots];
    std::int16_t matched[ship_capacity][anchor_slots];
    std::uint8_t cur_n[ship_capacity] = {}, prev_n[ship_capacity] = {};
    for (unsigned i = 0; i < ship_capacity; ++i)
        for (unsigned k = 0; k < anchor_slots; ++k) matched[i][k] = -1;
    // The draw into a ship's set: the root's own draw in front, others behind while there is room; a node and handle
    // already in the set keeps its first draw.
    const auto collect = [&](std::int16_t* set, std::uint8_t& n, unsigned i, bool root) {
        const LoggedDraw& d = log.draws[i];
        for (unsigned k = 0; k < n; ++k)
            if (log.draws[set[k]].node == d.node && log.draws[set[k]].handle == d.handle) return;
        if (root) {
            const unsigned keep = n < anchor_slots ? n : anchor_slots - 1;
            for (unsigned k = keep; k > 0; --k) set[k] = set[k - 1];
            set[0] = std::int16_t(i);
            n = std::uint8_t(keep + 1);
        } else if (n < anchor_slots)
            set[n++] = std::int16_t(i);
    };
    const auto anchor_to = [&](Light& l, const std::int16_t* set, unsigned n) {
        l.anchor_count = n;
        for (unsigned k = 0; k < n; ++k) {
            const LoggedDraw& d = log.draws[set[k]];
            l.anchors[k].node = d.node;
            l.anchors[k].handle = d.handle;
            for (unsigned j = 0; j < 12; ++j) l.anchors[k].rows[j] = d.world[j];
        }
    };
    const bool any_prev = prev.count != 0;
    for (unsigned i = 0; i < log.count; ++i) {
        const LoggedDraw& d = log.draws[i];
        // The draw's ship as build_nodes attributes it once the held entries are in: by its node first, then by its
        // parent (a prev ship found here has no entry in cur, or cur's probe of the same key would have found it).
        int c = find_ship(*cur, d.node), p = -1;
        bool root = c >= 0;
        if (c < 0 && any_prev) {
            p = find_ship(prev, d.node);
            root = p >= 0;
        }
        if (c < 0 && p < 0) {
            c = find_ship(*cur, d.parent);
            if (c < 0 && any_prev) p = find_ship(prev, d.parent);
        }
        if (c >= 0) {
            collect(drawn_cur[c], cur_n[c], i, root);
            continue;
        }
        if (p < 0) continue;
        collect(drawn_prev[p], prev_n[p], i, root);
        const Light& was = prev.lights[p];
        for (unsigned k = 0; k < was.anchor_count; ++k)
            if (matched[p][k] < 0 && was.anchors[k].node == d.node && was.anchors[k].handle == d.handle)
                matched[p][k] = std::int16_t(i);
    }
    for (unsigned c = 0; c < cur->count; ++c) {
        Light& l = cur->lights[c];
        anchor_to(l, drawn_cur[c], cur_n[c]);
        l.bound_brightness = l.brightness;
    }
    for (unsigned p = 0; p < prev.count; ++p) {
        const Light& was = prev.lights[p];
        if (!was.anchor_count || find_ship(*cur, was.root) >= 0) continue;
        bool covered = false;
        for (unsigned w = 0; w < walked_count && !covered; ++w) covered = walked[w] == was.root;
        if (covered) {
            ++cur->stats.hold_walked;
            continue;
        }
        const unsigned age = was.age + 1;
        if (age > window) {
            ++cur->stats.hold_expired;
            continue;
        }
        // The first old anchor drawn again whose earlier rows invert; none: the ship is not drawn, no hold.
        double inverse[9];
        int use = -1;
        for (unsigned k = 0; k < was.anchor_count && use < 0; ++k)
            if (matched[p][k] >= 0 && invert_rows(was.anchors[k].rows, inverse)) use = int(k);
        if (use < 0) continue;
        const float* from = was.anchors[use].rows;
        const LoggedDraw& d = log.draws[matched[p][use]];
        // M = W' W^-1: P' = M (P - t) + t'.
        double m[9];
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned k = 0; k < 3; ++k)
                m[r * 3 + k] = double(d.world[r * 4]) * inverse[k] + double(d.world[r * 4 + 1]) * inverse[3 + k] +
                               double(d.world[r * 4 + 2]) * inverse[6 + k];
        const auto carry = [&](const double at[3], double to[3]) {
            const double rel[3] = {at[0] - double(from[3]), at[1] - double(from[7]), at[2] - double(from[11])};
            for (unsigned r = 0; r < 3; ++r)
                to[r] = m[r * 3] * rel[0] + m[r * 3 + 1] * rel[1] + m[r * 3 + 2] * rel[2] + double(d.world[r * 4 + 3]);
        };
        const float fade = hold_fade(age, window), brightness = was.bound_brightness * fade;
        unsigned index = cur->count;
        if (cur->count >= ship_capacity) {
            const int victim = dimmest_ship(*cur);
            ++cur->stats.dropped; // the held entry or the entry it replaces
            if (victim < 0 || !(was.own || brightness > cur->lights[victim].brightness)) continue;
            erase_ship_slot(*cur, unsigned(find_ship_slot(*cur, cur->lights[victim].root)));
            index = unsigned(victim);
        } else
            ++cur->count;
        Light& l = cur->lights[index];
        static_cast<LightFields&>(l) = was; // own kept
        l.brightness = brightness;
        l.bound_brightness = was.bound_brightness;
        carry(was.position, l.position);
        l.plate_count = was.plate_count;
        for (unsigned q = 0; q < was.plate_count; ++q) {
            l.plates[q] = was.plates[q];
            carry(was.plates[q].position, l.plates[q].position);
        }
        anchor_to(l, drawn_prev[p], prev_n[p]);
        l.age = age;
        l.fade = fade;
        unsigned h = hash_key(was.root, 0, ship_slots - 1);
        while (cur->slot[h]) h = (h + 1) & (ship_slots - 1);
        cur->slot[h] = std::uint16_t(index + 1);
        if (cur->blocks_known) rescan_ship_block(*cur, index / ship_block);
        ++cur->stats.held;
    }
}
// The node table for the next frame: every logged draw whose node is a lit ship's root or hangs directly under one.
// A held ship's light and plates are scaled by its fade (colour, radius and value_eff; 1 for a ship with a record).
inline void build_nodes(const ShipTable& ships, const DrawLog& log, NodeTable* out) noexcept {
    out->clear();
    out->stats.logged = log.count;
    out->stats.log_dropped = log.dropped;
    if (!ships.count) return;
    bool lit[ship_capacity] = {};
    for (unsigned i = 0; i < log.count; ++i) {
        const LoggedDraw& d = log.draws[i];
        int ship = find_ship(ships, d.node);
        if (ship < 0) ship = find_ship(ships, d.parent);
        if (ship < 0) continue;
        if (find_node(*out, d.node, d.handle)) continue; // the node's first draw of the frame places it
        ++out->stats.matched;
        double inverse[9];
        if (!invert_rows(d.world, inverse)) {
            ++out->stats.singular;
            continue;
        }
        if (out->count >= node_capacity) {
            ++out->stats.dropped;
            continue;
        }
        const Light& l = ships.lights[ship];
        const float fade = l.fade;
        const double rel[3] = {l.position[0] - double(d.world[3]), l.position[1] - double(d.world[7]),
                               l.position[2] - double(d.world[11])};
        // Built in place in the next free entry (a refused node leaves it free): every field the readers use is written
        // here, the plate arrays up to plate_count (no zero fill or copy of the 2,056-byte entry per node).
        NodeLight& n = out->nodes[out->count];
        n.node = d.node;
        n.handle = d.handle;
        n.plate_count = 0;
        bool finite = true;
        for (unsigned k = 0; k < 3; ++k) {
            const double v = inverse[k * 3] * rel[0] + inverse[k * 3 + 1] * rel[1] + inverse[k * 3 + 2] * rel[2];
            n.local[k] = float(v);
            n.colour[k] = l.colour[k] * fade;
            finite = finite && ee::finite_f(n.local[k]);
        }
        n.radius = l.radius * fade;
        // The plates in the node's model space; a non-finite one is left out (its nozzle keeps the full gain).
        for (unsigned p = 0; p < l.plate_count; ++p) {
            const Plate& plate = l.plates[p];
            const double r[3] = {plate.position[0] - double(d.world[3]), plate.position[1] - double(d.world[7]),
                                 plate.position[2] - double(d.world[11])};
            float* out_plate = n.plate[n.plate_count];
            const float value = plate.value * fade;
            bool ok = ee::finite_f(value) && value > 0.f;
            for (unsigned k = 0; k < 3; ++k) {
                out_plate[k] = float(inverse[k * 3] * r[0] + inverse[k * 3 + 1] * r[1] + inverse[k * 3 + 2] * r[2]);
                ok = ok && ee::finite_f(out_plate[k]);
            }
            out_plate[3] = value;
            for (unsigned k = 0; k < 3; ++k) n.plate_colour[n.plate_count][k] = plate.colour[k] * fade;
            n.plate_count += ok;
        }
        if (!finite) {
            ++out->stats.singular;
            continue;
        }
        unsigned h = hash_key(d.node, d.handle, node_slots - 1);
        while (out->slot[h]) h = (h + 1) & (node_slots - 1);
        out->slot[h] = std::uint16_t(++out->count);
        if (!lit[ship]) {
            lit[ship] = true;
            ++out->ships;
        }
    }
}

// --------------------------------------------------------------------------- per draw
// The vertex program's register layout for the world rows and the view-inverse rows (camera position in .w, the
// camera's axes in the columns), by original VS fingerprint: the light-loop programs read world c28-30 and the view
// inverse c34-36, the single-light ones c7-9 and c13-15 (verification/results/engine-light/eye_normal_registers.py).
struct VertexLayout {
    std::uint16_t world = 0, view_inverse = 0;
};
inline VertexLayout vertex_layout(std::uint64_t vs) noexcept {
    static const std::uint64_t loop[] = {
        0x53a0a641107ed76cull, 0x719856ce0c213220ull, 0x4944d81dfe531b37ull, 0x44c4a41ca92ae2e3ull,
        0x494fe349b8bc12ecull, 0xb0602757fce6e870ull, 0x0c223ad11bce02d5ull, 0x167eb2d5629ab9d3ull,
        0x330ceb9dd874ede2ull, 0x29d7c575396ed280ull, 0xa420a010b0271479ull, 0x57392213f62fef19ull,
        0x5c17a381b149b3b9ull, 0x37e6956afd8b8d76ull, 0x2e0254dd999841c2ull, 0x33388c8897d428a5ull,
        0xb4059ab6af8fc529ull, 0x37c34a7478544c14ull, 0xc30104cb0efb6675ull, 0xe2ad860d5fbb3e59ull};
    static const std::uint64_t single[] = {0xbadefd5143b3024full, 0x19a246a56e9d9700ull, 0x233d17d26ce0c1fcull,
                                           0x12b8a13f13fe8cfeull, 0xea3d15b287892410ull, 0xa804f173f693944aull,
                                           0xa7cddf2c98d61117ull, 0x2a560f246c90fa64ull, 0x74fdc00d802b4027ull};
    for (const auto h : loop)
        if (h == vs) return {28, 34};
    for (const auto h : single)
        if (h == vs) return {7, 13};
    return {};
}
// One light's two registers for one draw: (L - cam, R^2), (colour, 1 / R^2), L the model-space point `local` placed by
// the draw's world rows, cam the view-inverse rows' translation. false on a non-positive or non-finite R^2 or a
// non-finite result (out then partly written).
inline bool light_registers(const float local[3], const float colour[3], float radius, const float world[12],
                            const float view_inverse[12], float out[8]) noexcept {
    double rel[3];
    for (unsigned i = 0; i < 3; ++i) {
        const double placed = double(world[i * 4]) * local[0] + double(world[i * 4 + 1]) * local[1] +
                              double(world[i * 4 + 2]) * local[2];
        rel[i] = placed + (double(world[i * 4 + 3]) - double(view_inverse[i * 4 + 3]));
    }
    const float r2 = radius * radius;
    if (!(r2 > 0.f) || !ee::finite_f(r2)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        out[i] = float(rel[i]);
        out[4 + i] = colour[i];
    }
    out[3] = r2;
    out[7] = 1.f / r2;
    for (unsigned i = 0; i < 8; ++i)
        if (!ee::finite_f(out[i])) return false;
    return true;
}
// The plate tiers (the twin's uniform branches on c202.w; renderer/linear_engine_light_inc.h): tier k runs the first
// plate_runs[k] slots, the smallest run that holds the ship's plates: one plate runs slot 0 alone (tier 0, the light
// c200-c201 as before the plates), then steps of four slots to sixteen and of eight to plate_slots, so a ship runs at
// most three pad slots below sixteen plates and seven above (the Split Ocelot's ten: twelve).
constexpr unsigned plate_tiers = 12;
constexpr unsigned plate_runs[plate_tiers] = {1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72};
static_assert(plate_runs[plate_tiers - 1] == plate_slots, "the last tier runs every slot");
inline unsigned plate_tier_index(unsigned plates) noexcept {
    unsigned k = 0;
    while (k + 1 < plate_tiers && plate_runs[k] < plates) ++k;
    return k;
}
inline float plate_tier(unsigned plates) noexcept {
    return float(int(plate_tier_index(plates)));
}
constexpr bool same_runs(const unsigned (&runs)[plate_tiers]) noexcept {
    for (unsigned k = 0; k < plate_tiers; ++k)
        if (runs[k] != plate_runs[k]) return false;
    return true;
}
inline unsigned plate_run(unsigned plates) noexcept {
    return plate_runs[plate_tier_index(plates)];
}
// The per-draw staging block mirroring c(block_first)-c202 (renderer::EngineLightAbi, asserted where it is uploaded):
// slot p's plate register ((P_p - cam) / v_p, 1 / v_p) at plate_register(p) (c197 for slot 0, c197 - 2p after it) and,
// for p >= 1, its light colour (colour_p, 1 / R_p^2) at colour_register(p) = c198 - 2p, directly above its plate; the
// rows c198-c199 never uploaded (the twins' DEFs); plate 0's light, the camera's forward axis and the tier c200-c202
// (block_light: draw_constants). A run of r slots occupies the 2r - 1 registers from run_first(r) to c197, so a draw
// uploads the plates in use and nothing above them in one call (block_upload_run). The light of slot p >= 1 is not
// stored: L_p is the plate's point, so the twin forms L_p - cam = ((P_p - cam) / v_p) v_p and R_p^2 = 1 / (1 / R_p^2)
// from the two registers once, for the selected plate (three registers per plate, the layout before the cap of 72,
// would hold at most 49 between c50 and c202; two hold 72).
constexpr unsigned block_top = 197, block_first = block_top + 2 - 2 * plate_slots, block_registers = 203 - block_first;
constexpr unsigned block_light = (200 - block_first) * 4, block_floats = block_registers * 4;
constexpr unsigned plate_register(unsigned p) noexcept {
    return block_top - 2 * p;
}
constexpr unsigned colour_register(unsigned p) noexcept { // p >= 1
    return block_top + 1 - 2 * p;
}
constexpr unsigned plate_offset(unsigned p) noexcept {
    return (plate_register(p) - block_first) * 4;
}
constexpr unsigned colour_offset(unsigned p) noexcept {
    return (colour_register(p) - block_first) * 4;
}
constexpr unsigned run_first(unsigned run) noexcept {
    return block_top + 2 - 2 * run;
}
static_assert(block_first == 55 && plate_register(plate_slots - 1) == block_first && colour_register(1) == 196 &&
                  run_first(1) == block_top && block_light / 4 + block_first == 200,
              "c55-c197 the plates and their lights, c200-c202 the light");
// The three registers c200-c202 for one draw: the light (plate 0: the ship's brightest nozzle) placed by the draw's
// world rows, relative to the camera of its view-inverse rows; (colour, 1 / R^2); the camera's forward axis (.w: the
// tier, block_constants). false (nothing to upload) on non-finite rows, a degenerate forward axis or a non-finite
// result.
inline bool draw_constants(const NodeLight& n, const float world[12], const float view_inverse[12],
                           float out[12]) noexcept {
    float f[3] = {view_inverse[2], view_inverse[6], view_inverse[10]};
    const float f2 = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
    if (!(f2 > 1e-12f) || !ee::finite_f(f2)) return false;
    const float inv = 1.f / scalar::sqrt(f2);
    if (!light_registers(n.local, n.colour, n.radius, world, view_inverse, out)) return false;
    for (unsigned i = 0; i < 3; ++i) out[8 + i] = f[i] * inv;
    out[11] = 0.f;
    for (unsigned i = 8; i < 12; ++i)
        if (!ee::finite_f(out[i])) return false;
    return true;
}
// One plate register for one draw: ((P - cam) / v, 1 / v), P the model-space plate point `local` (value_eff v in .w)
// placed by the draw's world rows like the light; the twin forms (d / v)^2 = |D / v - (P - cam) / v|^2 for the pixel at
// D (camera-relative). false when not finite or 1 / v not positive (out then partly written).
inline bool plate_register_values(const float local[4], const float world[12], const float view_inverse[12],
                                  float out[4]) noexcept {
    const double inv = 1. / double(local[3]);
    bool ok = true;
    for (unsigned i = 0; i < 3; ++i) {
        const double placed = double(world[i * 4]) * local[0] + double(world[i * 4 + 1]) * local[1] +
                              double(world[i * 4 + 2]) * local[2];
        out[i] = float((placed + (double(world[i * 4 + 3]) - double(view_inverse[i * 4 + 3]))) * inv);
        ok = ok && ee::finite_f(out[i]);
    }
    out[3] = float(inv);
    return ok && ee::finite_f(out[3]) && out[3] > 0.f;
}
// A further plate's light colour register: (colour, 1 / R^2), R = reach x value_eff, as draw_constants gives plate 0's
// c201. false on a non-positive or non-finite R^2 or a non-finite result.
inline bool plate_colour_values(const float colour[3], float value, float out[4]) noexcept {
    const float radius = reach * value, r2 = radius * radius;
    if (!(r2 > 0.f) || !ee::finite_f(r2)) return false;
    for (unsigned i = 0; i < 3; ++i) out[i] = colour[i];
    out[3] = 1.f / r2;
    for (unsigned i = 0; i < 4; ++i)
        if (!ee::finite_f(out[i])) return false;
    return true;
}
// An unused plate register: (d / v)^2 = 4 everywhere, weight 0 (slot 0 of a ship whose first plate is not finite).
constexpr float unused_plate[4] = {2.f, 0.f, 0.f, 0.f};
// The whole block for one draw: draw_constants (false: nothing to upload, the draw stays unlit), the tier in c202.w,
// and the slots of the tier's run: slot 0's plate register and, per further slot p, its plate register and its light
// colour from its own nozzle (plate_register_values, plate_colour_values). A slot of the run without a plate, or whose
// registers are not finite, is a pad: slot 0's plate register and plate 0's colour (c201), so its (d / v)^2 equals slot
// 0's and the selection, which keeps the earlier slot on a tie, never takes it, nor does the weight change. A ship whose
// first plate is not finite runs slot 0 alone (tier 0) with the unused plate: plate 0's light, no plate weight. Slots
// beyond the run are not written (nor uploaded: block_upload_run). One plate costs one draw_constants and one plate
// register as before; each further plate one plate register (about 12 double multiply-adds) and one colour.
inline bool block_constants(const NodeLight& n, const float world[12], const float view_inverse[12],
                            float out[block_floats]) noexcept {
    float* light = out + block_light;
    if (!draw_constants(n, world, view_inverse, light)) return false;
    float* key0 = out + plate_offset(0);
    const bool first = n.plate_count && plate_register_values(n.plate[0], world, view_inverse, key0);
    if (!first)
        for (unsigned k = 0; k < 4; ++k) key0[k] = unused_plate[k];
    const unsigned tier = first ? plate_tier_index(n.plate_count) : 0u, run = plate_runs[tier];
    light[11] = float(int(tier));
    for (unsigned p = 1; p < run; ++p) {
        float* key = out + plate_offset(p);
        float* colour = out + colour_offset(p);
        if (p < n.plate_count && plate_register_values(n.plate[p], world, view_inverse, key) &&
            plate_colour_values(n.plate_colour[p], n.plate[p][3], colour))
            continue;
        for (unsigned k = 0; k < 4; ++k) {
            key[k] = key0[k];
            colour[k] = light[4 + k];
        }
    }
    return true;
}
// The run a block's draw uploads (from its tier in c202.w): the registers run_first(run) .. c197, 2 run - 1 of them,
// then c200-c202; a one-plate draw uploads c197 alone below the light.
inline unsigned block_upload_run(const float block[block_floats]) noexcept {
    const int tier = int(block[block_light + 11]);
    return plate_runs[tier > 0 && tier < int(plate_tiers) ? tier : 0];
}
// --------------------------------------------------------------------------- twin kinds
// The original-shading variants a twin may stand in for (MotionOutput::engine_light_kinds order): the plain motion
// variant, the fill, the gained, the gained widened, the share producer, the share gained, the share gained widened.
// Each base is created only when its transform applied exactly these options (share / light-map gain / widening), so a
// twin is bound in its place only when the twin's transform applied the same three: the light's fill block can change
// the share plan (it is planned with the block's site), and a twin whose share, gain or widening differs from its
// base would change more than the light.
struct TwinOptions {
    bool share, gain, widen;
};
constexpr unsigned twin_kinds = 7;
constexpr TwinOptions twin_options[twin_kinds] = {{false, false, false}, {false, false, false}, {false, true, false},
                                                  {false, true, true},   {true, false, false},  {true, true, false},
                                                  {true, true, true}};
// The twin of kind `kind` stands in for its base: the light applied and the transform's share / gain / widen out-flags
// equal the base's options.
inline bool twin_matches_base(unsigned kind, bool engine, bool share, bool gain, bool widen) noexcept {
    if (kind >= twin_kinds || !engine) return false;
    const TwinOptions& o = twin_options[kind];
    return share == o.share && gain == o.gain && widen == o.widen;
}
// --------------------------------------------------------------------------- frame row
struct FrameCounts {
    unsigned candidates = 0;    // routed draws whose node carries a light
    unsigned draws_lit = 0;     // of them: bound a twin and uploaded the light
    unsigned no_twin = 0;       // the selected program has no twin (refused at creation, or a program kind without one)
    unsigned no_rows = 0;       // world / view-inverse rows unknown, the VS layout unknown, or non-finite constants
};
} // namespace x3m::engine_light::core
