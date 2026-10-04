// The engine light tables with nozzle plates (docs/architecture/engine-light.md "Nozzle plates"), host cost before and
// after: build_ships over 1,024 main records (128 ships x 8 nozzles; after: the plates full, merge_layers included),
// build_nodes (128 ships x 4 nodes) and the per-draw constants (draw_constants, and plate_constants with -DPLATES).
// Best of 200 table builds; 2,000,000 draw lookups (all hits). Build against a header:
//   after:  clang++ -std=c++17 -O2 -DPLATES -DENGINE_LIGHT_HEADER='"<repo>/src/proxy/engine_light_core.h"' plate_table_cost.cpp
//   before: the same without -DPLATES against `git show 4f036d43:src/proxy/engine_light_core.h` (its includes resolved
//           from the checkout's src/proxy).
// Output: plate_table_cost_out.txt (arm64 host, three runs each).
#include ENGINE_LIGHT_HEADER
#include <chrono>
#include <cstdio>
#include <memory>
using namespace x3m::engine_light::core;
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;
using clk = std::chrono::steady_clock;
int main() {
    auto ships = std::make_unique<ShipTable>();
    auto nodes = std::make_unique<NodeTable>();
    auto log = std::make_unique<DrawLog>();
    const ep::Look look{};
    static ee::Record r[1024];
    static std::uint32_t parent[1024];
    for (unsigned i = 0; i < 1024; ++i) {
        r[i] = ee::Record{};
        const unsigned ship = i / 8, nozzle = i % 8;
        r[i].origin[0] = float(ship) * 1000.f + float(nozzle) * 40.f;
        r[i].origin[1] = float(nozzle) * 7.f;
        r[i].axis[2] = 1.f;
        r[i].size = 10.f + float(nozzle);
        r[i].s = .5f;
        r[i].node_handle = 1000 + i;
        r[i].body = -1;
        parent[i] = 0x100000u + ship * 64u;
    }
    double best_ships = 1e9, best_nodes = 1e9;
    for (unsigned rep = 0; rep < 200; ++rep) {
        auto b = clk::now();
        build_ships(r, parent, nullptr, nullptr, nullptr, 0, 1024, nullptr, look, 1.f, ships.get());
        double us = std::chrono::duration<double, std::micro>(clk::now() - b).count();
        if (us < best_ships) best_ships = us;
    }
    float w[12] = {1, 0, 0, 1000, 0, 1, 0, 2000, 0, 0, 1, 3000};
    log->clear();
    for (unsigned i = 0; i < 128; ++i)
        for (unsigned k = 0; k < 4; ++k) log->push(0x900000u + i * 64u + k * 4u, 0x100000u + i * 64u, k, w);
    for (unsigned rep = 0; rep < 200; ++rep) {
        auto b = clk::now();
        build_nodes(*ships, *log, nodes.get());
        double us = std::chrono::duration<double, std::micro>(clk::now() - b).count();
        if (us < best_nodes) best_nodes = us;
    }
    float vi[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float c[64];
    volatile float sink = 0.f;
    constexpr unsigned rounds = 2000000;
    unsigned hits = 0;
    auto b = clk::now();
    for (unsigned j = 0; j < rounds; ++j) {
        const unsigned i = (j * 2654435761u) % 128u;
        const NodeLight* n = find_node(*nodes, 0x900000u + i * 64u + (j & 3u) * 4u, j & 3u);
        if (n && draw_constants(*n, w, vi, c + 40)) {
#ifdef PLATES
            plate_constants(*n, w, vi, c);
#endif
            ++hits;
            sink = sink + c[40] + c[0];
        }
    }
    double ns = std::chrono::duration<double, std::nano>(clk::now() - b).count() / rounds;
    std::printf("ships=%u ships_us=%.2f nodes=%u nodes_us=%.2f hits=%u draw_ns=%.2f\n", ships->count, best_ships,
                nodes->count, best_nodes, hits, ns);
}
