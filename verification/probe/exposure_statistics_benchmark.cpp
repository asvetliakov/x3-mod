// Original deterministic CPU workload. No D3D, readback, game data or FPS claim.
// Allocation, data construction and cached spatial weights are outside timing.
#include "../../src/renderer/exposure.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace x3m::renderer;
int main() {
    constexpr unsigned batches = 41, repetitions = 40, warmup = 20;
    const unsigned sizes[][2] = {{80, 48}, {80, 23}, {128, 128}};
    ExposureParams params;
    for (const auto& size : sizes) for (unsigned dense = 0; dense != 2; ++dense) {
        const unsigned count = size[0] * size[1];
        std::vector<float> means(count), maxima(count), weights(count);
        std::vector<TileSample> scratch(count);
        unsigned expected_lit = 0, state = 0x1734ab29u;
        for (unsigned i = 0; i != count; ++i) {
            state = state * 1664525u + 1013904223u;
            const bool lit = dense || i % 10 == 0;
            expected_lit += lit;
            means[i] = lit ? -8.f + float(state & 65535u) * (13.f / 65535.f) : -13.f;
            maxima[i] = std::min(6.f, means[i] + float((state >> 16) & 255u) / 128.f);
        }
        tile_weights(weights.data(), size[0], size[1], params.meter_edge_weight);
        auto run = [&] { return meter_statistics(means.data(), maxima.data(), weights.data(), count,
                                                 scratch.data(), params); };
        const MeterStatistics expected = run();
        if (expected.tiles != count || expected.lit != expected_lit || expected.neutral ||
            !std::isfinite(expected.lit_median_log) || !std::isfinite(expected.p99_max_log)) return 2;
        float witness = 0;
        for (unsigned i = 0; i != warmup; ++i) witness += run().lit_median_log;
        std::vector<double> samples;
        samples.reserve(batches);
        for (unsigned batch = 0; batch != batches; ++batch) {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i != repetitions; ++i) {
                const auto result = run();
                // Observable result validation prevents dead-code removal. The
                // few comparisons/addition are included in the diagnostic time.
                if (result.lit != expected_lit || result.lit_median_log != expected.lit_median_log ||
                    result.p99_max_log != expected.p99_max_log) return 3;
                witness += result.lit_median_log;
            }
            const auto end = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::micro>(end - start).count() / repetitions);
        }
        std::sort(samples.begin(), samples.end());
        if (!std::isfinite(witness) || samples.front() <= 0) return 4;
        std::printf("CASE width=%u height=%u dense=%u tiles=%u lit=%u batches=%u repetitions=%u warmup=%u median_us=%.6f p95_us=%.6f min_us=%.6f witness=%.6f\n",
                    size[0], size[1], dense, count, expected_lit, batches, repetitions, warmup,
                    samples[batches / 2], samples[(batches * 95) / 100], samples.front(), double(witness));
    }
    std::puts("RESULT PASS cases=6");
}
