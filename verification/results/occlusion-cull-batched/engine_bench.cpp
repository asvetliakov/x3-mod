// Host microbenchmark of the proxy-side engine-skip work per frame (docs/verification/occlusion-cull.md, 2026-10-08
// "Engine-side skip"): clang++ -std=c++17 -O2 -I src/proxy verification/results/occlusion-cull-batched/engine_bench.cpp -o /tmp/engine_bench && /tmp/engine_bench
// Measured 2026-10-08 (arm64 host, native): ledger_300_draws_plus_publish_us=0.85 lookups_400_us=0.41.
// The proxy-side cost per frame (ledger of 300 scene draws over 250 nodes, 60 of
// them fully skipped, then one publish) and of the stub's logic mirrored in C++ over 400 pass visits.
#include "occlusion_engine_core.h"
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace x3m::occlusion_cull::engine;
int main() {
    static Ledger l; static Table t;
    const std::int32_t pos[3] = {120000, -30000, 500000};
    auto read = [&](std::uintptr_t, void* out, std::size_t) { std::memcpy(out, pos, 12); return true; };
    std::uint32_t nodes[250];
    for (unsigned i = 0; i < 250; ++i) nodes[i] = 0x41000000u + i * 0x150u;
    const unsigned frames = 20000;
    volatile unsigned sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (unsigned f = 1; f <= frames; ++f) {
        l.begin(f, 0x41c68200u);
        for (unsigned d = 0; d < 300; ++d) {
            const unsigned i = d % 250;
            Ledger::Slot* s = l.draw(nodes[i]);
            if (i < 60) l.skipped(s, 900000 + i, read);
        }
        PublishStats st{};
        sink += t.publish(l, f + 1, 8, &st);
    }
    auto t1 = std::chrono::steady_clock::now();
    for (unsigned f = 1; f <= frames; ++f) {
        for (unsigned v = 0; v < 400; ++v) sink += unsigned(t.lookup(0x41000000u + (v * 0x150u) % (250 * 0x150u), 900000 + v % 250, pos, frames + 1));
    }
    auto t2 = std::chrono::steady_clock::now();
    const double us_ledger = std::chrono::duration<double, std::micro>(t1 - t0).count() / frames;
    const double us_lookup = std::chrono::duration<double, std::micro>(t2 - t1).count() / frames;
    std::printf("BENCH ledger_300_draws_plus_publish_us=%.2f lookups_400_us=%.2f (host, clang -O2, sink=%u)\n", us_ledger, us_lookup, unsigned(sink));
    return 0;
}
