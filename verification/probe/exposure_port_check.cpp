// Host driver for verification/analysis/test_exposure_port.py: compiled
// natively (no D3D, no Wine) against src/renderer/exposure.cpp, it prints the
// port's results for the cases the test replays through
// tools/analysis/exposure_reference.py. Each line: "<case> <values...>".
#include "../../src/renderer/exposure.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace x3m::renderer;

int main() {
    const ExposureParams p{};
    std::printf("params %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", double(p.key), double(p.ev_offset), double(p.ev_min), double(p.ev_max),
                double(p.tau_up), double(p.tau_down), double(p.dt_min), double(p.dt_max), double(p.meter_floor), double(p.meter_clip));
    // Metering: decode modes and clamps on a few engine-space pixels.
    const float pixels[][3] = {{.18f, .18f, .18f}, {1.f, .8f, .9f}, {100.f, 100.f, 100.f}, {0.f, 0.f, 0.f}, {.001f, .002f, .003f}, {4.f, 16.f, 1.f}};
    const ExposureDecode modes[] = {ExposureDecode::Gamma22, ExposureDecode::Srgb, ExposureDecode::None};
    for (unsigned m = 0; m < 3; ++m)
        for (const auto& px : pixels)
            std::printf("meter %u %.9g %.9g %.9g %.9g %u\n", m, double(px[0]), double(px[1]), double(px[2]), double(meter_level0(px, modes[m], p)), unsigned(meter_clipped(px, modes[m], p)));
    // The chain on a 13x7 image (odd sizes: edge-clamped taps) and 16x16 (exact mean).
    for (unsigned size : {13u * 7u, 16u * 16u}) {
        const unsigned w = size == 91 ? 13 : 16, h = size == 91 ? 7 : 16;
        std::vector<float> level(size), scratch(size);
        for (unsigned i = 0; i < size; ++i) level[i] = float(-6.0 + 0.1 * double(i % 17) + 0.01 * double(i));
        const double mean = reduce_mean(level.data(), size);
        std::printf("chain %u %u %.9g %.9g\n", w, h, mean, double(reduce_chain(level.data(), scratch.data(), w, h)));
    }
    // Targets, clamps, rates, steps.
    for (double avg : {-5.443855, -0.246413, -2.582891, -40., 40., 0.}) std::printf("target %.9g %.9g\n", avg, double(ev_target(float(avg), p)));
    for (double dt : {0.001, 1. / 240., 0.016, 0.033, 0.2, 3.0}) std::printf("dt %.9g %.9g %.9g %.9g\n", dt, double(clamp_dt(float(dt), p)), double(adapt_rate(float(dt), p.tau_up)), double(adapt_rate(float(dt), p.tau_down)));
    for (double ev : {-8., -2., 0., 0.5, 3.}) std::printf("exposure %.9g %.9g %.9g\n", ev, double(exposure_multiplier(float(ev))), double(taa_k(exposure_multiplier(float(ev)))));
    std::printf("resolve %.9g %.9g\n", double(resolve_ev(ExposureMode::Manual, -2.f, 1.5f)), double(resolve_ev(ExposureMode::Auto, -2.f, 1.5f)));
    // The frame loop: 30 frames of the fixture's script at 16 ms, then 10 at 33 ms with an offset.
    {
        ExposureState s(p);
        std::printf("simulate");
        for (unsigned f = 0; f < 30; ++f) {
            const float avg = f < 10 ? -5.443855f : f < 20 ? -0.246413f : -2.582891f;
            std::printf(" %.9g", double(s.ev()));
            s.step(avg, 0.016f);
        }
        std::printf(" %.9g\n", double(s.ev()));
        ExposureParams q = p; q.ev_offset = 1.f; q.tau_up = .2f; q.tau_down = .6f;
        ExposureState o(q);
        std::printf("simulate_offset");
        for (unsigned f = 0; f < 10; ++f) { std::printf(" %.9g", double(o.ev())); o.step(f < 5 ? -5.443855f : -0.246413f, 0.033f); }
        std::printf(" %.9g\n", double(o.ev()));
    }
    // Weighting round trip.
    float rgb[3] = {2.f, 40.f, .5f};
    weight_color(rgb, 1.5f);
    std::printf("weighted %.9g %.9g %.9g\n", double(rgb[0]), double(rgb[1]), double(rgb[2]));
    unweight_color(rgb, 1.5f);
    std::printf("unweighted %.9g %.9g %.9g %.9g\n", double(rgb[0]), double(rgb[1]), double(rgb[2]), double(luma_weight(.5f, 2.f)));
    return 0;
}
