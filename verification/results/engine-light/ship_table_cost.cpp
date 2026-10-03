// The ship table's cost at the cap (docs/architecture/engine-light.md, "CPU path and latency"): build_ships over a
// full ring of 1,024 main-jet records of distinct ships, in ascending brightness (every newcomer past 256 replaces the
// dimmest entry: the worst order) and in descending brightness (every newcomer refused). Prints the best of 200 runs.
//   clang++ -std=c++17 -O2 verification/results/engine-light/ship_table_cost.cpp -o /tmp/ship_table_cost && /tmp/ship_table_cost
// 2026-10-03, arm64 host (measured): ascending 23.2 us, descending 9.0 us (the whole-table rescan before the block
// minima: 123.9 us ascending).
#include "../../../src/proxy/engine_light_core.h"
#include <chrono>
#include <cstdio>
#include <memory>
using namespace x3m::engine_light::core;
namespace ee = x3m::engine_effects::core;
int main() {
    auto ships = std::make_unique<ShipTable>();
    static ee::Record r[1024];
    static std::uint32_t parent[1024];
    static std::uint8_t own[1024];
    const x3m::engine_plumes::Look look{};
    for (int order = 0; order < 2; ++order) {
        for (unsigned i = 0; i < 1024; ++i) {
            ee::Record x{};
            x.origin[0] = float(i);
            x.axis[2] = -1.f;
            const unsigned rank = order ? 1023 - i : i;
            x.size = 10.f + float(rank) * .125f;
            x.s = .5f;
            x.z = 1.f;
            x.node_handle = i;
            x.flags = std::uint16_t(ee::white << ee::cluster_shift);
            x.body = -1;
            r[i] = x;
            parent[i] = 0x10000u + i * 16u;
            own[i] = 0;
        }
        double best = 1e9;
        for (int rep = 0; rep < 200; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            build_ships(r, parent, nullptr, nullptr, nullptr, 0, 1024, nullptr, look, 1.f, ships.get(), own);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            if (us < best) best = us;
        }
        std::printf("%s 1024 records: %.1f us (best of 200), ships %u dropped %u\n", order ? "descending" : "ascending", best,
                    ships->count, ships->stats.dropped);
    }
}
