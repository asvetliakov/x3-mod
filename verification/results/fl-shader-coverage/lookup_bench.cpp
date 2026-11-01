// Shader-setter lookup cost of part 4b (host micro-benchmark, not game FPS): the
// pair contract (pixels/pairs and the XT rows) plus the hull-program lookup that
// the proxy runs once per shader bind, for an unknown pair (full scans), the last
// FL conventional pair, an FL XT pair and an AP pair. Best of 5 runs of 2e6 calls.
// lookup_bench.py builds it against the base commit and the working tree.
#include "linear_emission.h"
#include "linear_material.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
using namespace x3m::renderer;
int main() {
    const struct { const char* name; std::uint64_t vs, ps; } cases[] = {
        {"unknown", 0x1111111111111111ull, 0x2222222222222222ull},
        {"fl_conventional_last", 0x4944d81dfe531b37ull, 0xe326b2b1363c9886ull},
        {"fl_xt", 0x37c34a7478544c14ull, 0xe57c48fc06198e20ull},
        {"ap_pair", 0x53a0a641107ed76cull, 0x63f96eba9eea7880ull}};
    for (const auto& c : cases) {
        double best = 1e30;
        for (unsigned run = 0; run < 5; ++run) {
            volatile unsigned sink = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 2000000; ++i)
                sink = sink + linear_material_pair_reviewed(c.vs, c.ps) + linear_emission_hull_program_reviewed(c.ps);
            const double ns =
                std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 2000000;
            best = ns < best ? ns : best;
        }
        std::printf("%s %.1f\n", c.name, best);
    }
}
