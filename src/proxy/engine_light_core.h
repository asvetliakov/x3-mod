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
// (renderer/linear_engine_light_inc.h, c200-c202). No Windows dependency: the host tests compile it. No x87: float
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
// records with one frame of latency.
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
// light-map gain suppression of the twin, the light's own nozzle among them. Each plate is the point its nozzle's light
// would sit at (behind x value_eff along the plume axis) with that nozzle's value_eff; the twin's weight is the maximum
// over the ship's plates. Brightest first (I(s) x value_eff; ties: the lower node handle, then the earlier record);
// one plate per node handle; a smaller co-located layer of another record (engine_plumes merge_layers) is a plate at
// its natural value_eff (no floor), as the plume stage draws it.
constexpr unsigned plate_slots = 8;
struct Plate {
    double position[3]{};           // world, the record's space (the nozzle's light point)
    float value = 0.f;              // the nozzle's value_eff (world units)
    float brightness = 0.f;         // I(s) x value_eff: the order key
    std::uint32_t handle = 0;
};
struct Light {
    std::uint32_t root = 0;         // the ship's root node (the jets' node+0x18)
    double position[3]{};           // world, the record's space
    float colour[3]{};              // linear
    float radius = 0.f;             // world units
    float brightness = 0.f;         // I(s) x value_eff: the selection key
    std::uint32_t handle = 0;       // the chosen jet's node handle (ties: the lower one wins)
    float s = 0.f, value = 0.f;     // the chosen jet's throttle and value_eff (the row's diagnostics)
    bool own = false;               // a record of the own ship fed it (never evicted by a full table)
    unsigned plate_count = 0;
    Plate plates[plate_slots];      // the ship's main nozzles, brightest first (add_plate)
};
struct ShipStats {
    // dropped: lights lost to the full table (a dimmer newcomer refused, or an entry evicted by a brighter or own one);
    // unfloored: main-jet records at their natural value as a smaller co-located layer (still a plate); plates_dropped:
    // main nozzles beyond the ship's plate_slots (the dimmest give way)
    unsigned records = 0, main = 0, rcs = 0, brake = 0, other_view = 0, invalid = 0, orphan = 0, dropped = 0;
    unsigned unfloored = 0, plates_dropped = 0;
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
                         float preset_scale, Light* out) noexcept {
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
    Light l{};
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
inline void add_plate(Light& ship, const Light& record, ShipStats& stats) noexcept {
    Plate p{};
    for (unsigned i = 0; i < 3; ++i) p.position[i] = record.position[i];
    p.value = record.value;
    p.brightness = record.brightness;
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
// it; the light itself is chosen over all main records as before.
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
        Light l;
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
        out->lights[index] = l;
        out->lights[index].plate_count = 0;
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
// The node table for the next frame: every logged draw whose node is a lit ship's root or hangs directly under one.
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
        const double rel[3] = {l.position[0] - double(d.world[3]), l.position[1] - double(d.world[7]),
                               l.position[2] - double(d.world[11])};
        NodeLight n{};
        n.node = d.node;
        n.handle = d.handle;
        bool finite = true;
        for (unsigned k = 0; k < 3; ++k) {
            const double v = inverse[k * 3] * rel[0] + inverse[k * 3 + 1] * rel[1] + inverse[k * 3 + 2] * rel[2];
            n.local[k] = float(v);
            n.colour[k] = l.colour[k];
            finite = finite && ee::finite_f(n.local[k]);
        }
        n.radius = l.radius;
        // The plates in the node's model space; a non-finite one is left out (its nozzle keeps the full gain).
        for (unsigned p = 0; p < l.plate_count; ++p) {
            const Plate& plate = l.plates[p];
            const double r[3] = {plate.position[0] - double(d.world[3]), plate.position[1] - double(d.world[7]),
                                 plate.position[2] - double(d.world[11])};
            float* out_plate = n.plate[n.plate_count];
            bool ok = ee::finite_f(plate.value) && plate.value > 0.f;
            for (unsigned k = 0; k < 3; ++k) {
                out_plate[k] = float(inverse[k * 3] * r[0] + inverse[k * 3 + 1] * r[1] + inverse[k * 3 + 2] * r[2]);
                ok = ok && ee::finite_f(out_plate[k]);
            }
            out_plate[3] = plate.value;
            n.plate_count += ok;
        }
        if (!finite) {
            ++out->stats.singular;
            continue;
        }
        unsigned h = hash_key(d.node, d.handle, node_slots - 1);
        while (out->slot[h]) h = (h + 1) & (node_slots - 1);
        out->nodes[out->count] = n;
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
// The three registers c200-c202 for one draw: the light placed by the draw's world rows, relative to the camera of its
// view-inverse rows; (colour, 1 / R^2); the camera's forward axis. false (nothing to upload) on non-finite rows, a
// degenerate forward axis or a non-finite result.
inline bool draw_constants(const NodeLight& n, const float world[12], const float view_inverse[12],
                           float out[12]) noexcept {
    double rel[3];
    for (unsigned i = 0; i < 3; ++i) {
        const double placed = double(world[i * 4]) * n.local[0] + double(world[i * 4 + 1]) * n.local[1] +
                              double(world[i * 4 + 2]) * n.local[2];
        rel[i] = placed + (double(world[i * 4 + 3]) - double(view_inverse[i * 4 + 3]));
    }
    float f[3] = {view_inverse[2], view_inverse[6], view_inverse[10]};
    const float f2 = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
    if (!(f2 > 1e-12f) || !ee::finite_f(f2)) return false;
    const float inv = 1.f / scalar::sqrt(f2);
    const float r2 = n.radius * n.radius;
    if (!(r2 > 0.f) || !ee::finite_f(r2)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        out[i] = float(rel[i]);
        out[4 + i] = n.colour[i];
        out[8 + i] = f[i] * inv;
    }
    out[3] = r2;
    out[7] = 1.f / r2;
    out[11] = 0.f;
    for (unsigned i = 0; i < 12; ++i)
        if (!ee::finite_f(out[i])) return false;
    return true;
}
// The plate registers (renderer::EngineLightAbi::plate_constant, plate_slots of them) for one draw: per plate
// ((P - cam) / v, 1 / v), P placed by the draw's world rows like the light, v its value_eff; the twin forms
// (d / v)^2 = |D / v - (P - cam) / v|^2 for the pixel at D (camera-relative). An unused slot, or a plate whose
// constants are not finite, gets unused_plate: (d / v)^2 = 4 everywhere, weight 0. `tier` (null: none) gets the
// twin's uniform branch tier for c202.w: 0 for at most one plate, 1 for two to four, 2 for five to eight (the twin runs
// 1, 4 or 8 slots).
constexpr float unused_plate[4] = {2.f, 0.f, 0.f, 0.f};
inline float plate_tier(unsigned plates) noexcept {
    return plates <= 1 ? 0.f : plates <= 4 ? 1.f : 2.f;
}
inline void plate_constants(const NodeLight& n, const float world[12], const float view_inverse[12],
                            float out[plate_slots * 4], float* tier = nullptr) noexcept {
    if (tier) *tier = plate_tier(n.plate_count);
    for (unsigned p = 0; p < plate_slots; ++p) {
        float* o = out + p * 4;
        for (unsigned k = 0; k < 4; ++k) o[k] = unused_plate[k];
        if (p >= n.plate_count) continue;
        const float* local = n.plate[p];
        const double inv = 1. / double(local[3]);
        float c[4];
        bool ok = true;
        for (unsigned i = 0; i < 3; ++i) {
            const double placed = double(world[i * 4]) * local[0] + double(world[i * 4 + 1]) * local[1] +
                                  double(world[i * 4 + 2]) * local[2];
            c[i] = float((placed + (double(world[i * 4 + 3]) - double(view_inverse[i * 4 + 3]))) * inv);
            ok = ok && ee::finite_f(c[i]);
        }
        c[3] = float(inv);
        if (!ok || !ee::finite_f(c[3]) || !(c[3] > 0.f)) continue;
        for (unsigned k = 0; k < 4; ++k) o[k] = c[k];
    }
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
