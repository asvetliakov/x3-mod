// Ship- and node-table build cost of the engine light's core (src/proxy/engine_light_core.h) before and after the plate
// cap of 72 (docs/verification/engine-light.md, "Plate cap"): the ship table over the ring's 1,024 records (1024 / m
// ships of m main nozzles, best of 50), and the node table over 256 ships x 4 hull nodes (1,024 logged draws) for ships
// of 1, 8 and (when the core holds them) 72 nozzles: the first build after allocation (page faults on the table) and
// the best of 50 warm builds. Build against a tree's src/ with -I:
//   clang++ -std=c++17 -O2 -I <tree> node_build_cost.cpp -o cost && ./cost
// (tree = the repository root, or `git archive d77c26dd src | tar -x -C <dir>` for the baseline). Host timings, not
// game FPS.
#include "src/proxy/engine_light_core.h"
#include <chrono>
#include <cstdio>
#include <memory>
using namespace x3m::engine_light::core;
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;
static ee::Record jet(float x, float y, float size, std::uint32_t handle) {
    ee::Record r{};
    r.origin[0] = x;
    r.origin[1] = y;
    r.axis[2] = -1.f;
    r.size = size;
    r.s = .5f;
    r.z = .25f + 1.75f * .5f;
    r.node_handle = handle;
    r.flags = std::uint16_t(ee::white << ee::cluster_shift);
    r.body = -1;
    return r;
}
int main() {
    static ee::Record r[256 * 72];
    static std::uint32_t parent[256 * 72];
    const unsigned counts[3] = {1, 8, 72};
    std::printf("{\"plate_slots\":%u,\"node_bytes\":%u,\"rows\":[", plate_slots, unsigned(sizeof(NodeLight)));
    bool first_row = true;
    for (unsigned m : counts) {
        if (m > plate_slots) continue;
        auto ships = std::make_unique<ShipTable>();
        auto nodes = std::make_unique<NodeTable>();
        auto log = std::make_unique<DrawLog>();
        for (unsigned i = 0; i < 256; ++i)
            for (unsigned k = 0; k < m; ++k) {
                r[i * m + k] = jet(float(i) * 1000.f + float(k) * 9.f, float(k) * 3.f, 10.f - float(k) / float(m), 5000 + i * m + k);
                parent[i * m + k] = 0x100000u + i * 64u;
            }
        double ring = 1e30;
        for (unsigned rep = 0; rep < 50; ++rep) {
            const auto s0 = std::chrono::steady_clock::now();
            build_ships(r, parent, nullptr, nullptr, nullptr, 0, (1024 / m) * m, nullptr, ep::Look{}, 1.f, ships.get());
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - s0).count();
            ring = us < ring ? us : ring;
        }
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 256 * m, nullptr, ep::Look{}, 1.f, ships.get());
        const float w[12] = {.995f, 0, .0998f, 1000, 0, 1, 0, 2000, -.0998f, 0, .995f, 3000};
        const float vi[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        log->clear();
        for (unsigned i = 0; i < 256; ++i)
            for (unsigned k = 0; k < 4; ++k) log->push(0x900000u + i * 64u + k * 4u, 0x100000u + i * 64u, k, w);
        auto t0 = std::chrono::steady_clock::now();
        build_nodes(*ships, *log, nodes.get());
        const double cold = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        double warm = 1e30;
        for (unsigned rep = 0; rep < 50; ++rep) {
            t0 = std::chrono::steady_clock::now();
            build_nodes(*ships, *log, nodes.get());
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            warm = us < warm ? us : warm;
        }
        std::printf("%s{\"plates\":%u,\"ships_1024_records_us\":%.1f,\"nodes\":%u,\"cold_us\":%.1f,\"warm_us\":%.1f}",
                    first_row ? "" : ",", ships->lights[0].plate_count, ring, nodes->count, cold, warm);
        first_row = false;
    }
    std::printf("]}\n");
    return 0;
}
