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
constexpr unsigned ship_capacity = 256;     // lights per frame (ships); further ships are counted dropped
constexpr unsigned ship_slots = 512;        // open addressing, a power of two above twice the capacity
constexpr unsigned log_capacity = 4096;     // routed hull draws logged per frame; further draws are counted dropped
constexpr unsigned node_capacity = 2048;    // hull nodes carrying a light; further nodes are counted dropped
constexpr unsigned node_slots = 4096;
struct Light {
    std::uint32_t root = 0;         // the ship's root node (the jets' node+0x18)
    double position[3]{};           // world, the record's space
    float colour[3]{};              // linear
    float radius = 0.f;             // world units
    float brightness = 0.f;         // I(s) x value_eff: the selection key
    std::uint32_t handle = 0;       // the chosen jet's node handle (ties: the lower one wins)
    float s = 0.f, value = 0.f;     // the chosen jet's throttle and value_eff (the row's diagnostics)
};
struct ShipStats {
    unsigned records = 0, main = 0, rcs = 0, brake = 0, other_view = 0, invalid = 0, orphan = 0, dropped = 0;
};
struct ShipTable {
    Light lights[ship_capacity];
    std::uint16_t slot[ship_slots]; // 0 empty, else index + 1
    unsigned count = 0;
    ShipStats stats{};
    void clear() noexcept {
        count = 0;
        stats = {};
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
// Ship lights from one frame's records: per root the brightest main nozzle (brightness I(s) x value_eff; ties: the
// lower node handle, then the earlier record), only records of the scene view (scene phase and the scene camera's
// handle, as the plume stage draws them; `camera`/`scene` null = every record) with a known parent. RCS (steering) and
// brake-pushed bodies never feed it. At most ship_capacity ships; a new ship beyond that is counted dropped.
using BodyLookup = const ee::Body* (*)(int index);
inline void build_ships(const ee::Record* records, const std::uint32_t* parents, const float* radii,
                        const std::uint32_t* camera, const std::uint8_t* scene, std::uint32_t scene_handle,
                        unsigned count, BodyLookup body, const ep::Look& look, float preset_scale,
                        ShipTable* out) noexcept {
    out->clear();
    if (!records || !parents) return;
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
        if (!record_light(r, b, radii ? radii[i] : 0.f, look, preset_scale, &l)) {
            ++out->stats.invalid;
            continue;
        }
        ++out->stats.main;
        l.root = root;
        const int found = find_ship(*out, root);
        if (found >= 0) {
            Light& have = out->lights[found];
            if (l.brightness > have.brightness || (l.brightness == have.brightness && l.handle < have.handle))
                have = l;
            continue;
        }
        if (out->count >= ship_capacity) {
            ++out->stats.dropped;
            continue;
        }
        unsigned h = hash_key(root, 0, ship_slots - 1);
        while (out->slot[h]) h = (h + 1) & (ship_slots - 1);
        out->lights[out->count] = l;
        out->slot[h] = std::uint16_t(++out->count);
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
// --------------------------------------------------------------------------- frame row
struct FrameCounts {
    unsigned candidates = 0;    // routed draws whose node carries a light
    unsigned draws_lit = 0;     // of them: bound a twin and uploaded the light
    unsigned no_twin = 0;       // the selected program has no twin (refused at creation, or a program kind without one)
    unsigned no_rows = 0;       // world / view-inverse rows unknown, the VS layout unknown, or non-finite constants
};
} // namespace x3m::engine_light::core
