// Host driver of the engine light's portable core (src/proxy/engine_light_core.h; docs/architecture/engine-light.md).
// Runs the scenarios of verification/analysis/test_engine_light.py on synthetic rings and draw logs and prints one JSON
// object: the option parser, the brightest-main-nozzle selection (RCS, brake, other views and unknown parents
// ignored), the 256-ship cap (the dimmest entry gives way to a brighter ship and always to the own ship; the hash stays
// consistent through the replacements), the twin kinds' out-flag match (a twin whose share plan failed only with the
// light is refused), the one-frame protocol with the motion compensation (the light rides on the hull node
// that moved and turned between the frames), the per-draw constants, the nozzle plates up to the cap of 72 (the Split
// Ocelot's ten, 72, 80 with eight dropped; every plate lit by its own light in the modelled twin), the transformers'
// slot budget rule, and the per-draw lookup + constants cost on the host (ships of 1, 8 and 72 nozzles). No Windows
// dependency, no game bytes.
#include "../../src/proxy/engine_light_core.h"
#include "../../src/renderer/ps3_slot_budget.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace x3m::engine_light::core;
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;

static ee::Record jet(float x, float y, float z, float size, float s, std::uint32_t handle, std::uint16_t flags = 0) {
    ee::Record r{};
    r.origin[0] = x;
    r.origin[1] = y;
    r.origin[2] = z;
    r.axis[2] = -1.f; // the plume extends along -z
    r.size = size;
    r.s = s;
    r.z = .25f + 1.75f * s;
    r.node_handle = handle;
    r.flags = std::uint16_t(flags | (ee::white << ee::cluster_shift));
    r.body = -1;
    return r;
}
// A rigid world: rotation about y by angle a, scale k, translation t (rows: world_i = dot(row_i.xyz, p) + row_i.w).
static void rows(double a, double k, const double t[3], float out[12]) {
    const double c = std::cos(a), s = std::sin(a);
    const double m[9] = {c * k, 0, s * k, 0, k, 0, -s * k, 0, c * k};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 3; ++j) out[i * 4 + j] = float(m[i * 3 + j]);
        out[i * 4 + 3] = float(t[i]);
    }
}
static void apply(const float r[12], const double p[3], double out[3]) {
    for (unsigned i = 0; i < 3; ++i)
        out[i] = double(r[i * 4]) * p[0] + double(r[i * 4 + 1]) * p[1] + double(r[i * 4 + 2]) * p[2] + double(r[i * 4 + 3]);
}
// The twin's selection and law (linear_engine_light_inc.h) in float over a draw's block: r12 / r13 start as plate 0's
// light c200 / c201; past tier 0, r12 becomes slot 0's plate register and each further slot of the run whose
// u_i = |D v_i^-1 - k_i|^2 is below the running minimum takes its plate register and colour (ties: the earlier); the
// selected plate then becomes a light again (L - cam = k.xyz / k.w, R^2 = 1 / colour.w); E = colour saturate(N . l)
// saturate(1 - d^2 / R^2)^2. `single`: plate 0's light alone (the rule before the light per plate). `chosen` (null:
// none) gets the selected slot.
static void shade(const float* block, const float d[3], const float normal[3], bool single, float out[3],
                  unsigned* chosen = nullptr) {
    const float* light = block + block_light;
    float pos[4], col[4];
    for (unsigned j = 0; j < 4; ++j) {
        pos[j] = light[j];
        col[j] = light[4 + j];
    }
    const unsigned run = block_upload_run(block);
    unsigned pick = 0;
    if (!single && run > 1) {
        const float* sel = block + plate_offset(0);
        float s = 0.f;
        for (unsigned i = 0; i < run; ++i) {
            const float* k = block + plate_offset(i);
            float u = 0.f;
            for (unsigned j = 0; j < 3; ++j) {
                const float x = d[j] * k[3] - k[j];
                u += x * x;
            }
            if (!i) {
                s = u;
                continue;
            }
            if (!(u - s >= 0.f)) {
                sel = k;
                pick = i;
                for (unsigned j = 0; j < 4; ++j) col[j] = block[colour_offset(i) + j];
            }
            s = u < s ? u : s;
        }
        const float v = 1.f / sel[3];
        for (unsigned j = 0; j < 3; ++j) pos[j] = sel[j] * v;
        pos[3] = 1.f / col[3];
    }
    if (chosen) *chosen = pick;
    float to[3], d2 = 0.f;
    for (unsigned j = 0; j < 3; ++j) {
        to[j] = pos[j] - d[j];
        d2 += to[j] * to[j];
    }
    const float inv = 1.f / std::sqrt(d2 > 0x1p-40f ? d2 : 0x1p-40f);
    float ndl = 0.f;
    for (unsigned j = 0; j < 3; ++j) ndl += normal[j] * to[j] * inv;
    ndl = ndl < 0.f ? 0.f : ndl > 1.f ? 1.f : ndl;
    float q = (pos[3] - d2) * col[3];
    q = q < 0.f ? 0.f : q > 1.f ? 1.f : q;
    for (unsigned j = 0; j < 3; ++j) out[j] = col[j] * (q * q * ndl);
}
// The light point of slot p of a block (camera-relative): c200 for slot 0, else k.xyz / k.w.
static void slot_light(const float* block, unsigned p, float out[3]) {
    const float* k = block + plate_offset(p);
    for (unsigned j = 0; j < 3; ++j) out[j] = p ? k[j] / k[3] : block[block_light + j];
}
// A ship of `count` main nozzles under one root, its node drawn with rows `w` and camera `vi`: every plate lit at a hull
// point `beside` units from its light (toward +y, the normal toward the light) by its own light (the selection picks the
// slot), and the radiance there with plate 0's light alone. Prints the case's JSON object (no trailing comma).
static void many_nozzles(const char* name, const ee::Record* r, unsigned count, std::uint32_t root, const float w[12],
                         const float vi[12], float beside, ShipTable& ships, NodeTable& nodes, DrawLog& log) {
    static std::uint32_t parent[1024];
    for (unsigned i = 0; i < count; ++i) parent[i] = root;
    build_ships(r, parent, nullptr, nullptr, nullptr, 0, count, nullptr, ep::Look{}, 1.f, &ships);
    log.clear();
    log.push(root + 0x100, root, 4, w);
    build_nodes(ships, log, &nodes);
    const NodeLight* n = find_node(nodes, root + 0x100, 4);
    static float b[block_floats];
    for (float& x : b) x = -1.f;
    const bool ok = n && block_constants(*n, w, vi, b);
    const int a = find_ship(ships, root);
    const Light& l = ships.lights[a < 0 ? 0 : a];
    unsigned lit = 0, own = 0, dark_before = 0, pads = 0;
    float least = 1e30f;
    const float normal[3] = {0.f, -1.f, 0.f};
    for (unsigned p = 0; p < l.plate_count && ok; ++p) {
        float at[3], e[3], e0[3];
        slot_light(b, p, at);
        const float d[3] = {at[0], at[1] + beside, at[2]};
        unsigned chosen = 0;
        shade(b, d, normal, false, e, &chosen);
        shade(b, d, normal, true, e0);
        lit += e[0] > .5f;
        own += chosen == p;
        dark_before += e0[0] == 0.f;
        least = e[0] < least ? e[0] : least;
    }
    const unsigned run = ok ? block_upload_run(b) : 0u;
    for (unsigned p = l.plate_count; p < run; ++p)
        pads += std::memcmp(b + plate_offset(p), b + plate_offset(0), 16) == 0 &&
                std::memcmp(b + colour_offset(p), b + block_light + 4, 16) == 0;
    bool beyond = true; // registers below the run neither written nor uploaded
    for (unsigned i = 0; ok && i < (run_first(run) - block_first) * 4; ++i) beyond = beyond && b[i] == -1.f;
    std::printf("\"%s\":{\"ok\":%d,\"count\":%u,\"plates_dropped\":%u,\"node_plates\":%u,\"tier\":%.1f,\"run\":%u,"
                "\"upload_first\":%u,\"upload_count\":%u,\"lit\":%u,\"own_light\":%u,\"dark_before\":%u,\"least\":%.6f,"
                "\"pads\":%u,\"beyond_untouched\":%d,\"first_handles\":[%u,%u],\"last_handles\":[%u,%u]}",
                name, ok, l.plate_count, ships.stats.plates_dropped, n ? n->plate_count : 0u, double(b[block_light + 11]),
                run, run_first(run), 2 * run - 1, lit, own, dark_before, double(least), pads, beyond, l.plates[0].handle,
                l.plates[1].handle, l.plates[l.plate_count - 2].handle, l.plates[l.plate_count - 1].handle);
}
int main() {
    auto ships = std::make_unique<ShipTable>();
    auto nodes = std::make_unique<NodeTable>();
    auto log = std::make_unique<DrawLog>();
    const ep::Look look{};
    std::printf("{");
    // ---- option
    {
        Mode m = Mode::off;
        const bool on = parse_mode("on", 2, &m) && m == Mode::on;
        const bool off = parse_mode("off", 3, &m) && m == Mode::off;
        Mode keep = Mode::on;
        const bool refused = !parse_mode("On", 2, &keep) && !parse_mode("1", 1, &keep) && !parse_mode("off ", 4, &keep) &&
                             !parse_mode("", 0, &keep) && keep == Mode::on;
        std::printf("\"option\":{\"on\":%d,\"off\":%d,\"refused\":%d,\"default\":\"%s\"},", on, off, refused,
                    mode_name(default_mode));
    }
    // ---- selection: one ship (root 0x1000) with two main jets, an RCS jet and a brake-pushed jet; a second ship's jet
    // from another camera; an orphan jet (parent 0).
    {
        ee::Record r[7] = {jet(0, 0, -10, 10, .5f, 11),
                           jet(5, 0, -10, 10, 1.f, 12),                      // brightest main: s 1
                           jet(9, 9, -10, 40, 1.f, 13, ee::flag_steering),   // RCS: never, however bright
                           jet(7, 0, -10, 40, 1.f, 14, ee::flag_brake),      // brake-pushed: never
                           jet(0, 0, 0, 10, 1.f, 21),                        // ship 2, other camera
                           jet(0, 0, 0, 10, 1.f, 31),                        // orphan
                           jet(-5, 0, -10, 10, 1.f, 10)};                    // a tie with 12: the lower handle wins
        const std::uint32_t parent[7] = {0x1000, 0x1000, 0x1000, 0x1000, 0x2000, 0, 0x1000};
        const float radii[7] = {};
        const std::uint32_t camera[7] = {7, 7, 7, 7, 8, 7, 7};
        const std::uint8_t scene[7] = {1, 1, 1, 1, 1, 1, 1};
        build_ships(r, parent, radii, camera, scene, 7, 7, nullptr, look, 1.f, ships.get());
        const int i = find_ship(*ships, 0x1000);
        const Light& l = ships->lights[i < 0 ? 0 : i];
        std::printf("\"selection\":{\"ships\":%u,\"found\":%d,\"handle\":%u,\"position\":[%.6f,%.6f,%.6f],\"colour\":[%.6f,%.6f,%.6f],\"radius\":%.6f,\"brightness\":%.6f,\"main\":%u,\"rcs\":%u,\"brake\":%u,\"other_view\":%u,\"orphan\":%u,\"other_ship\":%d},",
                    ships->count, i, l.handle, l.position[0], l.position[1], l.position[2], double(l.colour[0]),
                    double(l.colour[1]), double(l.colour[2]), double(l.radius), double(l.brightness), ships->stats.main,
                    ships->stats.rcs, ships->stats.brake, ships->stats.other_view, ships->stats.orphan,
                    find_ship(*ships, 0x2000));
    }
    // ---- nozzle plates: ship A (root 0x1000) with three main nozzles (values 10, 8, 6 at s 1), a smaller co-located
    // layer of the first (size 5, 3 units behind it on its axis: unfloored, a plate at its natural value), a second
    // record of the 8 nozzle's handle at s 0 (dimmer: kept once, the brighter), an RCS jet; ship B (root 0x2000) with nine
    // main nozzles (values 10 + i, ties none): nine plates, none dropped (the cap is 72). Then the node table and the
    // per-draw block of ship A's node (identity rows translated, camera at the origin).
    {
        ee::Record r[16];
        std::uint32_t parent[16];
        unsigned n = 0;
        r[n] = jet(0, 0, 0, 10, 1.f, 50), parent[n++] = 0x1000;
        r[n] = jet(40, 0, 0, 8, 1.f, 51), parent[n++] = 0x1000;
        r[n] = jet(0, 40, 0, 6, 1.f, 52), parent[n++] = 0x1000;
        r[n] = jet(0, 0, -3, 5, 1.f, 53), parent[n++] = 0x1000;          // co-located inner layer of handle 50
        r[n] = jet(40, 0, 0, 8, 0.f, 51), parent[n++] = 0x1000;          // handle 51 again, idle
        r[n] = jet(9, 9, 9, 40, 1.f, 54, ee::flag_steering), parent[n++] = 0x1000;
        for (unsigned i = 0; i < 9; ++i) r[n] = jet(float(i) * 30.f, 500, 0, 10.f + float(i), 1.f, 60 + i), parent[n++] = 0x2000;
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, n, nullptr, look, 1.f, ships.get());
        const int a = find_ship(*ships, 0x1000), b = find_ship(*ships, 0x2000);
        const Light& la = ships->lights[a < 0 ? 0 : a];
        const Light& lb = ships->lights[b < 0 ? 0 : b];
        std::printf("\"plates\":{\"a\":{\"handle\":%u,\"count\":%u,\"handles\":[", la.handle, la.plate_count);
        for (unsigned i = 0; i < la.plate_count; ++i) std::printf("%s%u", i ? "," : "", la.plates[i].handle);
        std::printf("],\"values\":[");
        for (unsigned i = 0; i < la.plate_count; ++i) std::printf("%s%.6f", i ? "," : "", double(la.plates[i].value));
        std::printf("],\"first\":[%.6f,%.6f,%.6f],\"light\":[%.6f,%.6f,%.6f]},", la.plates[0].position[0],
                    la.plates[0].position[1], la.plates[0].position[2], la.position[0], la.position[1], la.position[2]);
        std::printf("\"b\":{\"handle\":%u,\"count\":%u,\"handles\":[", lb.handle, lb.plate_count);
        for (unsigned i = 0; i < lb.plate_count; ++i) std::printf("%s%u", i ? "," : "", lb.plates[i].handle);
        std::printf("]},\"unfloored\":%u,\"plates_dropped\":%u,", ships->stats.unfloored, ships->stats.plates_dropped);
        const double t[3] = {100., 200., 300.};
        float w[12];
        rows(0., 1., t, w);
        log->clear();
        log->push(0x5000, 0x1000, 9, w);
        build_nodes(*ships, *log, nodes.get());
        const NodeLight* node = find_node(*nodes, 0x5000, 9);
        float vi[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        static float blk[block_floats];
        for (float& v : blk) v = -1.f;
        const bool ok = node && block_constants(*node, w, vi, blk);
        // The run's plate registers ((P - cam) / v, 1 / v), slot 0 first, and slot 4 (beyond the run) not written.
        std::printf("\"ok\":%d,\"node_count\":%u,\"registers\":[", ok, node ? node->plate_count : 0u);
        for (unsigned p = 0; p < 4; ++p)
            for (unsigned k = 0; k < 4; ++k) std::printf("%s%.9g", p || k ? "," : "", double(blk[plate_offset(p) + k]));
        std::printf("],\"colours\":[");
        for (unsigned p = 1; p < 4; ++p)
            for (unsigned k = 0; k < 4; ++k) std::printf("%s%.9g", p > 1 || k ? "," : "", double(blk[colour_offset(p) + k]));
        const float tier = blk[block_light + 11];
        const bool beyond = blk[plate_offset(4)] == -1.f && blk[colour_offset(4)] == -1.f;
        // A non-finite plate (value 0 after the node build) is a pad: slot 0's plate register and plate 0's colour.
        NodeLight broken = node ? *node : NodeLight{};
        broken.plate[1][3] = 0.f;
        const bool broken_ok = node && block_constants(broken, w, vi, blk);
        const bool pad = broken_ok && std::memcmp(blk + plate_offset(1), blk + plate_offset(0), 16) == 0 &&
                         std::memcmp(blk + colour_offset(1), blk + block_light + 4, 16) == 0;
        // A non-finite first plate: tier 0, slot 0 the unused plate (plate 0's light alone, no plate weight).
        broken = node ? *node : NodeLight{};
        broken.plate[0][3] = 0.f;
        const bool first_ok = node && block_constants(broken, w, vi, blk);
        std::printf("],\"tier\":%.1f,\"beyond_untouched\":%d,\"broken_pad\":%d,\"broken_first\":[%d,%.1f,%.9g,%.9g,%.9g,%.9g],"
                    "\"tiers\":[",
                    double(tier), beyond, pad, first_ok, double(blk[block_light + 11]), double(blk[plate_offset(0)]),
                    double(blk[plate_offset(0) + 1]), double(blk[plate_offset(0) + 2]), double(blk[plate_offset(0) + 3]));
        for (unsigned c = 0; c <= plate_slots + 1; ++c) std::printf("%s%.0f", c ? "," : "", double(plate_tier(c)));
        std::printf("],\"runs\":[");
        for (unsigned c = 0; c <= plate_slots + 1; ++c) std::printf("%s%u", c ? "," : "", plate_run(c));
        std::printf("]},");
    }
    // ---- many nozzles under the cap of 72 (engine-light.md "Plate cap"; Run 135 A triage): the Split Ocelot's ten
    // main nozzles, 2 huge (value_eff 939.2) and 8 big3 (548.1), s 1, the big3 a brightness tie (handles 0x566 .. 0x56f,
    // the huge 0x564 / 0x565): ten plates, none dropped (eight before 2026-10-08 left 0x56e / 0x56f without one), tier
    // 3 (run 12, two pads), every plate lit by its own light at a hull point 300 units beside it. The cap: 72 nozzles
    // (value 10 + i / 8, 100 apart on a 9 x 8 grid, the hull point 5 units beside each): 72 plates, tier 11, run 72; 80
    // nozzles: the 72 brightest, 8 dropped. Rows of the node at the ship, camera 30,000 units in front looking +z.
    {
        const double t[3] = {2.0e4, -1.0e3, 6.0e4};
        float w[12];
        rows(0., 1., t, w);
        float vi[12] = {1, 0, 0, float(t[0]), 0, 1, 0, float(t[1]), 0, 0, 1, float(t[2] - 30000.)};
        static ee::Record r[80];
        for (unsigned i = 0; i < 10; ++i) {
            const float x = float(i % 5) * 2400.f - 4800.f, y = float(i / 5) * 2400.f;
            r[i] = jet(float(t[0]) + x, float(t[1]) + y, float(t[2]), i < 2 ? 939.2f : 548.1f, 1.f, 0x564 + i);
        }
        std::printf("\"many\":{");
        many_nozzles("ocelot", r, 10, 0x7000, w, vi, 300.f, *ships, *nodes, *log);
        for (unsigned i = 0; i < 80; ++i)
            r[i] = jet(float(t[0]) + float(i % 9) * 100.f, float(t[1]) + float(i / 9) * 100.f, float(t[2]),
                       10.f + float(i) * .125f, 1.f, 0x9000 + i);
        std::printf(",");
        many_nozzles("cap", r, 72, 0x7400, w, vi, 5.f, *ships, *nodes, *log);
        std::printf(",");
        many_nozzles("over", r, 80, 0x7800, w, vi, 5.f, *ships, *nodes, *log);
        std::printf("},");
    }
    // ---- a light per plate (user decision 2026-10-08): the Split Ocelot's two secondary big3 nozzles (Run 134 A
    // triage: equal value_eff 548.076, 2,411 apart, reach 3 x 548.076 = 1,644), here at s 1, axis -z. Both plates carry
    // their own light (plate 0 at c200-c201, plate 1 from its plate register c195 and colour c196), slots 2-3 pads; the
    // twin's selection and law modelled in float (shade) at a hull point 300 units beside each light: both lit, while
    // plate 0's light alone (the rule before) leaves the second dark. A one-nozzle ship: c200-c202 and c197 equal
    // draw_constants / plate_register_values bit for bit, nothing else written, the upload c197 alone, and the modelled
    // radiance with the selection equals the single light's bit for bit.
    {
        const float v = 548.076f;
        ee::Record r[2] = {jet(-1205.5f, 0, 0, v, 1.f, 81), jet(1205.5f, 0, 0, v, 1.f, 80)};
        const std::uint32_t parent[2] = {0x6000, 0x6000};
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 2, nullptr, look, 1.f, ships.get());
        const double t[3] = {1.0e4, -2.0e3, 5.0e4};
        float w[12];
        rows(0., 1., t, w);
        log->clear();
        log->push(0x6100, 0x6000, 4, w);
        build_nodes(*ships, *log, nodes.get());
        const NodeLight* n = find_node(*nodes, 0x6100, 4);
        // Camera 6,000 units in front of the ship (along -z), looking +z.
        float vi[12] = {1, 0, 0, float(t[0]), 0, 1, 0, float(t[1]), 0, 0, 1, float(t[2] - 6000.)};
        static float b[block_floats];
        for (float& x : b) x = -1.f;
        const bool ok = n && block_constants(*n, w, vi, b);
        const int a = find_ship(*ships, 0x6000);
        const Light& l = ships->lights[a < 0 ? 0 : a];
        // The hull points: 300 units beside each light point (toward +y), the normal toward the light.
        float radiance[2][3] = {}, before[2][3] = {}, at[2][3] = {};
        const float normal[3] = {0.f, -1.f, 0.f};
        for (unsigned p = 0; p < 2 && ok; ++p) {
            slot_light(b, p, at[p]);
            const float d[3] = {at[p][0], at[p][1] + 300.f, at[p][2]};
            shade(b, d, normal, false, radiance[p]);
            shade(b, d, normal, true, before[p]);
        }
        const float* k1 = b + plate_offset(1);
        const float* c1 = b + colour_offset(1);
        bool keys_pad = true, colours_pad = true;
        for (unsigned p = 2; p < 4; ++p) {
            keys_pad = keys_pad && std::memcmp(b + plate_offset(p), b + plate_offset(0), 16) == 0;
            colours_pad = colours_pad && std::memcmp(b + colour_offset(p), b + block_light + 4, 16) == 0;
        }
        std::printf("\"per_plate\":{\"ok\":%d,\"count\":%u,\"handles\":[%u,%u],\"light_handle\":%u,\"node_plates\":%u,"
                    "\"tier\":%.1f,\"r2\":[%.9g,%.9g],\"distance\":%.9g,\"colour_equal\":%d,\"keys_pad\":%d,\"colours_pad\":%d,"
                    "\"skipped_untouched\":%d,\"upload_run\":%u,\"radiance\":[%.9g,%.9g],\"before\":[%.9g,%.9g]},",
                    ok, l.plate_count, l.plates[0].handle, l.plates[1].handle, l.handle, n ? n->plate_count : 0u,
                    double(b[block_light + 11]), double(b[block_light + 3]), 1. / double(c1[3]),
                    std::sqrt(double(at[0][0] - at[1][0]) * double(at[0][0] - at[1][0]) +
                              double(at[0][1] - at[1][1]) * double(at[0][1] - at[1][1]) +
                              double(at[0][2] - at[1][2]) * double(at[0][2] - at[1][2])),
                    std::memcmp(b + block_light + 4, c1, 12) == 0, keys_pad, colours_pad,
                    b[plate_offset(4)] == -1.f && b[colour_offset(4)] == -1.f && b[0] == -1.f && k1[3] > 0.f,
                    block_upload_run(b), double(radiance[0][0]), double(radiance[1][0]), double(before[0][0]),
                    double(before[1][0]));
        // One nozzle: c197 and c200-c202 as before the change, nothing else written, the selection's radiance the
        // single light's.
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 1, nullptr, look, 1.f, ships.get());
        build_nodes(*ships, *log, nodes.get());
        const NodeLight* one = find_node(*nodes, 0x6100, 4);
        static float single[block_floats];
        float light[12], key[4];
        for (float& x : single) x = -1.f;
        const bool one_ok = one && block_constants(*one, w, vi, single) && draw_constants(*one, w, vi, light) &&
                            plate_register_values(one->plate[0], w, vi, key);
        light[11] = 0.f; // tier 0
        bool untouched = true; // every register but c197 and c200-c202: neither written nor uploaded
        for (unsigned i = 0; i < block_light; ++i)
            if (i < plate_offset(0) || i >= plate_offset(0) + 4) untouched = untouched && single[i] == -1.f;
        unsigned same = 0, checked = 0;
        for (int y = -40; y <= 40 && one_ok; ++y)
            for (int x = -40; x <= 40; ++x) {
                const float d[3] = {single[block_light] + 40.f * float(x), single[block_light + 1] + 40.f * float(y),
                                    single[block_light + 2] + 100.f};
                float e0[3], e1[3];
                shade(single, d, normal, false, e0);
                shade(single, d, normal, true, e1);
                ++checked;
                same += std::memcmp(e0, e1, 12) == 0;
            }
        std::printf("\"single\":{\"ok\":%d,\"light_identical\":%d,\"plates_identical\":%d,\"lights_untouched\":%d,"
                    "\"upload_run\":%u,\"upload_first\":%u,\"checked\":%u,\"radiance_identical\":%u},",
                    one_ok, one_ok && std::memcmp(single + block_light, light, 48) == 0,
                    one_ok && std::memcmp(single + plate_offset(0), key, sizeof key) == 0, untouched,
                    block_upload_run(single), run_first(block_upload_run(single)), checked, same);
    }
    // ---- the floor and a co-located layer (after Run 129 A): the Split Scorpion's nor 10 and tiny 5.04, 6.9 units
    // apart on one axis, one parent, R 67.3: the nor's plate at its floored value, the tiny's at its natural 5.04.
    {
        ee::Record r[2] = {jet(0, 0, 0, 10, 1.f, 70), jet(-5.7f, 3.8f, -1.f, 5.04f, 1.f, 71)};
        const std::uint32_t parent[2] = {0x3000, 0x3000};
        const float radii[2] = {67.3f, 67.3f};
        build_ships(r, parent, radii, nullptr, nullptr, 0, 2, nullptr, look, 1.f, ships.get());
        const int a = find_ship(*ships, 0x3000);
        const Light& l = ships->lights[a < 0 ? 0 : a];
        float nor = 0.f;
        ep::floored_value(look, r[0], radii[0], &nor);
        std::printf("\"layer_floor\":{\"count\":%u,\"values\":[%.6f,%.6f],\"handles\":[%u,%u],\"nor_floored\":%.6f,"
                    "\"unfloored\":%u},",
                    l.plate_count, double(l.plates[0].value), double(l.plates[1].value), l.plates[0].handle,
                    l.plates[1].handle, double(nor), ships->stats.unfloored);
    }
    // ---- cap: 300 ships, one jet each
    {
        static ee::Record r[300];
        static std::uint32_t parent[300];
        for (unsigned i = 0; i < 300; ++i) {
            r[i] = jet(float(i), 0, 0, 10, .5f, 100 + i);
            parent[i] = 0x10000u + i * 16u;
        }
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 300, nullptr, look, 1.f, ships.get());
        unsigned found = 0;
        for (unsigned i = 0; i < 300; ++i) found += find_ship(*ships, parent[i]) >= 0;
        std::printf("\"cap\":{\"ships\":%u,\"dropped\":%u,\"found\":%u},", ships->count, ships->stats.dropped, found);
    }
    // ---- cap with replacement: 299 other ships and the own ship last and dimmest (ring order), in ascending and in
    // descending brightness; the own ship is admitted either way, the other 255 entries are the brightest.
    {
        static ee::Record r[300];
        static std::uint32_t parent[300];
        static std::uint8_t own[300];
        std::printf("\"replace\":{");
        for (unsigned order = 0; order < 2; ++order) {
            for (unsigned i = 0; i < 299; ++i) {
                const unsigned rank = order ? 298 - i : i; // brightness rank: size 10 + rank / 8
                r[i] = jet(float(i), 0, 0, 10.f + float(rank) * .125f, .5f, 100 + i);
                parent[i] = 0x10000u + i * 16u;
                own[i] = 0;
            }
            r[299] = jet(0, 0, 0, 1.f, 0.f, 99);
            parent[299] = 0xf0000u;
            own[299] = 1;
            build_ships(r, parent, nullptr, nullptr, nullptr, 0, 300, nullptr, look, 1.f, ships.get(), own);
            unsigned found = 0, consistent = 0, dimmest_rank = 1000;
            for (unsigned i = 0; i < 299; ++i)
                if (find_ship(*ships, parent[i]) >= 0) {
                    ++found;
                    const unsigned rank = order ? 298 - i : i;
                    if (rank < dimmest_rank) dimmest_rank = rank;
                }
            for (unsigned i = 0; i < ships->count; ++i) consistent += find_ship(*ships, ships->lights[i].root) == int(i);
            const int o = find_ship(*ships, 0xf0000u);
            std::printf("\"%s\":{\"ships\":%u,\"dropped\":%u,\"own\":%d,\"own_flag\":%d,\"others\":%u,\"dimmest_rank\":%u,\"consistent\":%u}%s",
                        order ? "descending" : "ascending", ships->count, ships->stats.dropped, o >= 0,
                        o >= 0 && ships->lights[o].own, found, dimmest_rank, consistent, order ? "" : ",");
        }
        // Without the own tags (own = null) the same dimmest last ship is the one refused.
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 300, nullptr, look, 1.f, ships.get());
        std::printf(",\"untagged_own\":%d},", find_ship(*ships, 0xf0000u) >= 0);
    }
    // ---- twin kinds: the twin stands in for its base only with the base's share / gain / widen (a synthetic transform
    // outcome per kind: the expected flags, then each flag flipped; the share kinds with the share plan failing only
    // with the light, i.e. the twin reporting share 0).
    {
        unsigned accepted = 0, flipped_accepted = 0, share_lost = 0;
        for (unsigned k = 0; k < twin_kinds; ++k) {
            const TwinOptions o = twin_options[k];
            accepted += twin_matches_base(k, true, o.share, o.gain, o.widen);
            flipped_accepted += twin_matches_base(k, false, o.share, o.gain, o.widen);
            flipped_accepted += twin_matches_base(k, true, !o.share, o.gain, o.widen);
            flipped_accepted += twin_matches_base(k, true, o.share, !o.gain, o.widen);
            flipped_accepted += twin_matches_base(k, true, o.share, o.gain, !o.widen);
            if (o.share) share_lost += !twin_matches_base(k, true, false, o.gain, o.widen);
        }
        std::printf("\"twins\":{\"kinds\":%u,\"accepted\":%u,\"flipped_accepted\":%u,\"share_lost_refused\":%u,\"out_of_range\":%d},",
                    twin_kinds, accepted, flipped_accepted, share_lost, twin_matches_base(twin_kinds, true, false, false, false));
    }
    // ---- one-frame protocol with motion: frame N-1 has the jet record and the hull draw (node 0x5000 under root
    // 0x4000, rotated 0.3 rad, scale 2, at t0); frame N draws the same node rotated 0.5 rad at t1. Frame N-1 itself has
    // no table yet (nothing lit); frame N places the light with its own rows.
    {
        const double t0[3] = {1.0e5, -2.0e4, 1.5e5}, t1[3] = {1.0e5 + 25., -2.0e4, 1.5e5 - 40.};
        float w0[12], w1[12];
        rows(.3, 2., t0, w0);
        rows(.5, 2., t1, w1);
        // The nozzle at model point (0, 0, -20), its plume along model -z: in frame N-1 world.
        const double nozzle_model[3] = {0, 0, -20}, axis_model[3] = {0, 0, -1};
        double nozzle_world[3], axis_world[3], o[3];
        apply(w0, nozzle_model, nozzle_world);
        const double zero[3] = {0, 0, 0};
        apply(w0, zero, o);
        apply(w0, axis_model, axis_world);
        for (unsigned i = 0; i < 3; ++i) axis_world[i] = (axis_world[i] - o[i]) / 2.; // unit (scale 2)
        ee::Record r = jet(float(nozzle_world[0]), float(nozzle_world[1]), float(nozzle_world[2]), 8.f, 1.f, 77);
        for (unsigned i = 0; i < 3; ++i) r.axis[i] = float(axis_world[i]);
        const std::uint32_t parent = 0x4000;
        // Frame N-1: no table was built before it; the draw is logged.
        nodes->clear();
        const bool lit_before = find_node(*nodes, 0x5000, 9) != nullptr;
        log->clear();
        log->push(0x5000, 0x4000, 9, w0);
        log->push(0x5000, 0x4000, 9, w0); // a consecutive draw of the same node logs once
        log->push(0x6000, 0x7777, 3, w0); // another ship's node: not lit
        // Frame N begins: tables from N-1.
        build_ships(&r, &parent, nullptr, nullptr, nullptr, 0, 1, nullptr, look, 1.f, ships.get());
        build_nodes(*ships, *log, nodes.get());
        const NodeLight* n = find_node(*nodes, 0x5000, 9);
        const NodeLight* other = find_node(*nodes, 0x6000, 3);
        const NodeLight* wrong_handle = find_node(*nodes, 0x5000, 10);
        // Frame N's draw: camera at c, forward along +x (view-inverse rows: column 2 = forward).
        const double cam[3] = {1.0e5 - 300., -2.0e4 + 10., 1.5e5 + 5.};
        float vi[12] = {0, 0, 1, float(cam[0]), 0, 1, 0, float(cam[1]), -1, 0, 0, float(cam[2])};
        float c[12]{};
        const bool placed = n && draw_constants(*n, w1, vi, c);
        // Expected: the light's model point (nozzle + 0.5 value behind along model -z, in model units: value 8 world
        // units = 4 model units at scale 2) through frame N's rows, minus the camera.
        const double light_model[3] = {0, 0, -20 - .5 * 8. / 2.};
        double expect[3];
        apply(w1, light_model, expect);
        double error = 0.;
        for (unsigned i = 0; i < 3; ++i) {
            const double d = double(c[i]) - (expect[i] - cam[i]);
            error = d * d > error ? d * d : error;
        }
        std::printf("\"protocol\":{\"lit_before\":%d,\"logged\":%u,\"nodes\":%u,\"ships\":%u,\"found\":%d,\"other\":%d,\"wrong_handle\":%d,\"placed\":%d,\"error\":%.9g,\"constants\":[",
                    lit_before, log->count, nodes->count, nodes->ships, n != nullptr, other != nullptr,
                    wrong_handle != nullptr, placed, std::sqrt(error));
        for (unsigned i = 0; i < 12; ++i) std::printf("%s%.9g", i ? "," : "", double(c[i]));
        std::printf("],\"expected_rel\":[%.9g,%.9g,%.9g]},", expect[0] - cam[0], expect[1] - cam[1], expect[2] - cam[2]);
        // Degenerate rows: no constants.
        float bad[12] = {};
        const bool degenerate = n && !draw_constants(*n, w1, bad, c);
        log->clear();
        float singular[12] = {};
        log->push(0x5000, 0x4000, 9, singular);
        build_nodes(*ships, *log, nodes.get());
        std::printf("\"degenerate\":{\"view\":%d,\"singular_nodes\":%u,\"singular\":%u},", degenerate, nodes->count,
                    nodes->stats.singular);
    }
    // ---- the transformers' ps_3_0 slot budget (ps3_slot_budget.h): the device's cap, 32768 at or below the 512 spec
    // minimum and above 32768; 32768 before any device.
    {
        namespace r = x3m::renderer;
        std::printf("\"slot_budget\":{\"0\":%u,\"512\":%u,\"513\":%u,\"4096\":%u,\"32768\":%u,\"65535\":%u,\"default\":%u},",
                    r::ps3_slot_budget_for(0), r::ps3_slot_budget_for(512), r::ps3_slot_budget_for(513),
                    r::ps3_slot_budget_for(4096), r::ps3_slot_budget_for(32768), r::ps3_slot_budget_for(65535),
                    r::ps3_slot_budget());
    }
    // ---- layouts
    {
        const VertexLayout a = vertex_layout(0x4944d81dfe531b37ull), b = vertex_layout(0x233d17d26ce0c1fcull),
                           u = vertex_layout(0x1234ull);
        std::printf("\"layout\":{\"loop\":[%u,%u],\"single\":[%u,%u],\"unknown\":[%u,%u]},", a.world, a.view_inverse,
                    b.world, b.view_inverse, u.world, u.view_inverse);
    }
    // ---- host cost: 256 ships x 4 nodes in the table, lookups of hits and misses with the constants on hits
    {
        static ee::Record r[256];
        static std::uint32_t parent[256];
        for (unsigned i = 0; i < 256; ++i) {
            r[i] = jet(float(i) * 100.f, 0, 0, 10, .5f, 1000 + i);
            parent[i] = 0x100000u + i * 64u;
        }
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 256, nullptr, look, 1.f, ships.get());
        log->clear();
        const double t[3] = {1000., 2000., 3000.};
        float w[12];
        rows(.1, 1., t, w);
        for (unsigned i = 0; i < 256; ++i)
            for (unsigned k = 0; k < 4; ++k) log->push(0x900000u + i * 64u + k * 4u, parent[i], k, w);
        const auto build_begin = std::chrono::steady_clock::now();
        build_nodes(*ships, *log, nodes.get());
        const double build_us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - build_begin).count();
        float vi[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        float c[block_floats];
        unsigned hits = 0;
        constexpr unsigned rounds = 2000000;
        volatile float sink = 0.f;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned j = 0; j < rounds; ++j) {
            const unsigned i = (j * 2654435761u) % 512u; // half of them miss
            const NodeLight* n = find_node(*nodes, i < 256 ? 0x900000u + i * 64u : 0x7f00000u + i, i < 256 ? 0u : 1u);
            if (n && block_constants(*n, w, vi, c)) {
                ++hits;
                sink = sink + c[block_light];
            }
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / rounds;
        // The same lookups and the node build on ships of eight and of 72 main nozzles (every plate with its light): the
        // per-draw cost grows with the plates in use (the run); 256 ships x 4 nodes either way. The ship table's build
        // over the ring's 1,024 records: 128 ships x 8 nozzles and 14 ships x 72 (1,008 records).
        static ee::Record rk[256 * 72];
        static std::uint32_t parentk[256 * 72];
        double build_k[2] = {}, ns_k[2] = {}, ships_k[2] = {};
        unsigned hits_k[2] = {}, plates_k[2] = {};
        const unsigned per[2] = {8, 72};
        for (unsigned v = 0; v < 2; ++v) {
            const unsigned m = per[v];
            for (unsigned i = 0; i < 256; ++i)
                for (unsigned k = 0; k < m; ++k) {
                    rk[i * m + k] = jet(float(i) * 1000.f + float(k) * 9.f, float(k) * 3.f, 0, 10.f - float(k) / float(m),
                                        .5f, 5000 + i * m + k);
                    parentk[i * m + k] = parent[i];
                }
            // The ship table on the ring's capacity: best of 20 builds.
            const unsigned ring_ships = 1024 / m;
            ships_k[v] = 1e30;
            for (unsigned rep = 0; rep < 20; ++rep) {
                const auto sb = std::chrono::steady_clock::now();
                build_ships(rk, parentk, nullptr, nullptr, nullptr, 0, ring_ships * m, nullptr, look, 1.f, ships.get());
                const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - sb).count();
                ships_k[v] = us < ships_k[v] ? us : ships_k[v];
            }
            build_ships(rk, parentk, nullptr, nullptr, nullptr, 0, 256 * m, nullptr, look, 1.f, ships.get());
            build_k[v] = 1e30;
            for (unsigned rep = 0; rep < 5; ++rep) {
                const auto nb = std::chrono::steady_clock::now();
                build_nodes(*ships, *log, nodes.get());
                const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - nb).count();
                build_k[v] = us < build_k[v] ? us : build_k[v];
            }
            const auto begin_k = std::chrono::steady_clock::now();
            for (unsigned j = 0; j < rounds; ++j) {
                const unsigned i = (j * 2654435761u) % 512u;
                const NodeLight* n = find_node(*nodes, i < 256 ? 0x900000u + i * 64u : 0x7f00000u + i, i < 256 ? 0u : 1u);
                if (n && block_constants(*n, w, vi, c)) {
                    ++hits_k[v];
                    plates_k[v] = n->plate_count;
                    sink = sink + c[plate_offset(m - 1)];
                }
            }
            ns_k[v] = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin_k).count() / rounds;
        }
        std::printf("\"cost\":{\"nodes\":%u,\"ships\":%u,\"build_us\":%.3f,\"hits\":%u,\"rounds\":%u,\"ns_per_draw\":%.3f,"
                    "\"hits8\":%u,\"plates8\":%u,\"ns_per_draw8\":%.3f,\"build_us8\":%.3f,\"ships_us8\":%.3f,"
                    "\"hits72\":%u,\"plates72\":%u,\"ns_per_draw72\":%.3f,\"build_us72\":%.3f,\"ships_us72\":%.3f}",
                    nodes->count, nodes->ships, build_us, hits, rounds, ns, hits_k[0], plates_k[0], ns_k[0], build_k[0],
                    ships_k[0], hits_k[1], plates_k[1], ns_k[1], build_k[1], ships_k[1]);
    }
    std::printf("}\n");
    return 0;
}
