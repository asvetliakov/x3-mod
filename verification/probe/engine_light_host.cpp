// Host driver of the engine light's portable core (src/proxy/engine_light_core.h; docs/architecture/engine-light.md).
// Runs the scenarios of verification/analysis/test_engine_light.py on synthetic rings and draw logs and prints one JSON
// object: the option parser, the brightest-main-nozzle selection (RCS, brake, other views and unknown parents
// ignored), the 256-ship cap (the dimmest entry gives way to a brighter ship and always to the own ship; the hash stays
// consistent through the replacements), the twin kinds' out-flag match (a twin whose share plan failed only with the
// light is refused), the one-frame protocol with the motion compensation (the light rides on the hull node
// that moved and turned between the frames), the per-draw constants, and the per-draw lookup + constants cost on the
// host. No Windows dependency, no game bytes.
#include "../../src/proxy/engine_light_core.h"
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
        float c[12];
        unsigned hits = 0;
        constexpr unsigned rounds = 2000000;
        volatile float sink = 0.f;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned j = 0; j < rounds; ++j) {
            const unsigned i = (j * 2654435761u) % 512u; // half of them miss
            const NodeLight* n = find_node(*nodes, i < 256 ? 0x900000u + i * 64u : 0x7f00000u + i, i < 256 ? 0u : 1u);
            if (n && draw_constants(*n, w, vi, c)) {
                ++hits;
                sink = sink + c[0];
            }
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / rounds;
        std::printf("\"cost\":{\"nodes\":%u,\"ships\":%u,\"build_us\":%.3f,\"hits\":%u,\"rounds\":%u,\"ns_per_draw\":%.3f}",
                    nodes->count, nodes->ships, build_us, hits, rounds, ns);
    }
    std::printf("}\n");
    return 0;
}
