// Host driver for verification/analysis/test_exposure_port.py: compiled
// natively (no D3D, no Wine) against src/renderer/exposure.cpp, it prints the
// port's results for the cases the test replays through
// tools/analysis/exposure_reference.py. Each line: "<case> <values...>".
#include "../../src/renderer/exposure.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace x3m::renderer;

namespace {
// The synthetic tile images of the space-aware statistic (16x16 tiles of a
// 64x64 scene: 4x4 pixels each), in log2 units, mirrored by the test.
struct Scene {
    const char* name;
    std::vector<float> mean, max;
};
std::vector<Scene> scenes() {
    const float floor_log = -13.2877124f; // log2(1e-4)
    std::vector<Scene> out;
    // Uniform mid-grey (engine 0.18 -> decoded 0.0231, log2 -5.443855).
    out.push_back({"grey", std::vector<float>(256, -5.443855f), std::vector<float>(256, -5.443855f)});
    // Black sky with a 4x4-tile lit patch at engine 0.3 (log2 -3.820943).
    {
        Scene s{"sky", std::vector<float>(256, floor_log), std::vector<float>(256, floor_log)};
        for (unsigned y = 2; y < 6; ++y)
            for (unsigned x = 2; x < 6; ++x) s.mean[y * 16 + x] = s.max[y * 16 + x] = -3.820943f;
        out.push_back(s);
    }
    // Bright full frame (engine 1.0 -> log2 0).
    out.push_back({"menu", std::vector<float>(256, 0.f), std::vector<float>(256, 0.f)});
    // Mid-grey with five tiles holding four super-bright pixels each (clip 64 -> 6.0).
    {
        Scene s{"sparks", std::vector<float>(256, -5.443855f), std::vector<float>(256, -5.443855f)};
        for (unsigned i : {17u, 70u, 133u, 196u, 238u}) {
            s.mean[i] = (4.f * 6.f + 12.f * -5.443855f) / 16.f;
            s.max[i] = 6.f;
        }
        out.push_back(s);
    }
    // Black sky with one lit tile: below the minimum lit fraction, neutral.
    {
        Scene s{"speck", std::vector<float>(256, floor_log), std::vector<float>(256, floor_log)};
        s.mean[100] = s.max[100] = -2.f;
        out.push_back(s);
    }
    // A NaN tile among grey ones reads as the floor.
    {
        Scene s{"nan", std::vector<float>(256, -5.443855f), std::vector<float>(256, -5.443855f)};
        s.mean[5] = s.max[5] = __builtin_nanf("");
        out.push_back(s);
    }
    // A white emitter over the left half (96 tiles) and a centre object at the
    // key (64 tiles, log2 0.18 = -2.473931): the centre weighting lets the
    // object win the weighted median; unweighted the emitter would.
    {
        Scene s{"emitter", std::vector<float>(256, floor_log), std::vector<float>(256, floor_log)};
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 8; ++x) s.mean[y * 16 + x] = s.max[y * 16 + x] = 0.f;
        for (unsigned y = 4; y < 12; ++y)
            for (unsigned x = 4; x < 12; ++x) s.mean[y * 16 + x] = s.max[y * 16 + x] = -2.473931f;
        out.push_back(s);
    }
    return out;
}
std::vector<float> weights16() {
    std::vector<float> w(256);
    tile_weights(w.data(), 16, 16, ExposureParams{}.meter_edge_weight);
    return w;
}
MeterStatistics statistics(const Scene& s, const float* weights, const ExposureParams& p) {
    std::vector<TileSample> scratch(s.mean.size());
    return meter_statistics(s.mean.data(), s.max.data(), weights, unsigned(s.mean.size()), scratch.data(), p);
}
void print_statistics(const char* tag, const char* name, const MeterStatistics& m, const ExposureTarget& t) {
    std::printf("%s %s %u %u %.9g %.9g %.9g %.9g %.9g %u %.9g %.9g %.9g %.9g\n", tag, name, m.tiles, m.lit,
                double(m.avg_log_l), double(m.lit_fraction), double(m.lit_median_log), double(m.lit_mean_log),
                double(m.p99_max_log), unsigned(m.neutral), double(t.ev_key), double(t.ev_limit), double(t.ev_target),
                double(m.lit_weight));
}
} // namespace

int main() {
    const ExposureParams p{};
    std::printf("params %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %u\n",
                double(p.key), double(p.ev_offset), double(p.ev_min), double(p.ev_max), double(p.tau_up),
                double(p.tau_down), double(p.dt_min), double(p.dt_max), double(p.meter_floor), double(p.meter_clip),
                double(p.meter_bg), double(p.meter_min_lit), double(p.white_target), double(p.key_pull),
                double(p.ev_deadband), double(p.meter_edge_weight), double(kTonemapWhite), kMeterTileMax);
    // The centre weights of the 16x16 tile image.
    {
        const auto w = weights16();
        std::printf("weights");
        for (float v : w) std::printf(" %.9g", double(v));
        std::printf("\n");
    }
    // Metering: decode modes and clamps on a few engine-space pixels.
    const float pixels[][3] = {{.18f, .18f, .18f}, {1.f, .8f, .9f},       {100.f, 100.f, 100.f},
                               {0.f, 0.f, 0.f},    {.001f, .002f, .003f}, {4.f, 16.f, 1.f}};
    const ExposureDecode modes[] = {ExposureDecode::Gamma22, ExposureDecode::Srgb, ExposureDecode::None};
    for (unsigned m = 0; m < 3; ++m)
        for (const auto& px : pixels)
            std::printf("meter %u %.9g %.9g %.9g %.9g %u\n", m, double(px[0]), double(px[1]), double(px[2]),
                        double(meter_level0(px, modes[m], p)), unsigned(meter_clipped(px, modes[m], p)));
    // The chain on a 13x7 image (odd sizes: edge-clamped taps) and 16x16 (exact mean), down to 1x1.
    for (unsigned size : {13u * 7u, 16u * 16u}) {
        const unsigned w = size == 91 ? 13 : 16, h = size == 91 ? 7 : 16;
        std::vector<float> level(size), peak(size), scratch(size);
        for (unsigned i = 0; i < size; ++i) level[i] = peak[i] = float(-6.0 + 0.1 * double(i % 17) + 0.01 * double(i));
        const double mean = reduce_mean(level.data(), size);
        unsigned rw = w, rh = h;
        reduce_chain(level.data(), peak.data(), scratch.data(), rw, rh, 4, 1);
        std::printf("chain %u %u %.9g %.9g %.9g %u %u\n", w, h, mean, double(level[0]), double(peak[0]), rw, rh);
    }
    // The chain to a tile image: 40x24 level-0 values stop at 10x6 (tile_max 16).
    {
        const unsigned w = 40, h = 24;
        std::vector<float> level(w * h), peak(w * h), scratch(w * h);
        for (unsigned i = 0; i < w * h; ++i) {
            level[i] = float(-8.0 + 0.05 * double(i % 23));
            peak[i] = level[i] + float(0.5 * double(i % 3));
        }
        unsigned rw = w, rh = h;
        reduce_chain(level.data(), peak.data(), scratch.data(), rw, rh, 4, 16);
        std::printf("tiles %u %u", rw, rh);
        for (unsigned i = 0; i < rw * rh; ++i) std::printf(" %.9g %.9g", double(level[i]), double(peak[i]));
        std::printf("\n");
    }
    // The statistic and the target on the synthetic scenes, unweighted and centre-weighted.
    const auto weights = weights16();
    for (const auto& s : scenes()) {
        const MeterStatistics u = statistics(s, nullptr, p);
        print_statistics("stats", s.name, u, exposure_target(u, p));
        const MeterStatistics w = statistics(s, weights.data(), p);
        print_statistics("wstats", s.name, w, exposure_target(w, p));
    }
    // The key rule and the limit alone, the targets of uniform frames, the dead band.
    for (double v : {-5.443855, -0.246413, -2.582891, -13.2877124, 6.0, 0.0}) {
        std::vector<float> mean(256, float(v));
        std::vector<TileSample> scratch(256);
        const MeterStatistics m = meter_statistics(mean.data(), mean.data(), nullptr, 256, scratch.data(), p);
        std::printf("target %.9g %.9g %.9g %.9g\n", v, double(ev_key(float(v), p)), double(ev_limit(float(v), p)),
                    double(ev_target(m, p)));
    }
    for (double held : {0.0, 1.0, -2.0})
        for (double fresh : {0.0, 0.2, 0.25, 0.26, 1.1, 1.3, -1.8, -2.5, 2.0})
            std::printf("deadband %.9g %.9g %.9g %.9g\n", held, fresh,
                        double(apply_deadband(float(held), float(fresh), false, p)),
                        double(apply_deadband(float(held), float(fresh), true, p)));
    for (double dt : {0.001, 1. / 240., 0.016, 0.033, 0.2, 3.0})
        std::printf("dt %.9g %.9g %.9g %.9g\n", dt, double(clamp_dt(float(dt), p)),
                    double(adapt_rate(float(dt), p.tau_up)), double(adapt_rate(float(dt), p.tau_down)));
    for (double ev : {-3., -2., 0., 0.5, 2.})
        std::printf("exposure %.9g %.9g %.9g\n", ev, double(exposure_multiplier(float(ev))),
                    double(taa_k(exposure_multiplier(float(ev)))));
    std::printf("resolve %.9g %.9g\n", double(resolve_ev(ExposureMode::Manual, -2.f, 1.5f)),
                double(resolve_ev(ExposureMode::Auto, -2.f, 1.5f)));
    // The frame loop: 30 frames of uniform frames at 16 ms, then 10 at 33 ms with an offset.
    {
        auto uniform = [&](float v) {
            std::vector<float> mean(256, v);
            std::vector<TileSample> scratch(256);
            return meter_statistics(mean.data(), mean.data(), nullptr, 256, scratch.data(), p);
        };
        ExposureState s(p);
        std::printf("simulate");
        for (unsigned f = 0; f < 30; ++f) {
            const float avg = f < 10 ? -5.443855f : f < 20 ? -0.246413f : -2.582891f;
            std::printf(" %.9g", double(s.ev()));
            s.step(uniform(avg), 0.016f);
        }
        std::printf(" %.9g\n", double(s.ev()));
        // The dead band: small target changes hold, a large one moves (fresh, held target and EV after each step).
        ExposureState b(p);
        std::printf("simulate_deadband");
        const float levels[] = {-3.32f, -3.32f, -3.32f, -3.16f, -3.16f, -3.52f, -3.52f, -3.32f, -1.62f, -1.62f, -1.75f};
        for (float v : levels) {
            b.step(uniform(v), 0.2f);
            std::printf(" %.9g %.9g %.9g", double(b.ev_fresh()), double(b.ev_target()), double(b.ev()));
        }
        std::printf("\n");
        ExposureParams q = p;
        q.ev_offset = 1.f;
        q.tau_up = .2f;
        q.tau_down = .6f;
        ExposureState o(q);
        std::printf("simulate_offset");
        for (unsigned f = 0; f < 10; ++f) {
            std::printf(" %.9g", double(o.ev()));
            o.step(uniform(f < 5 ? -5.443855f : -0.246413f), 0.033f);
        }
        std::printf(" %.9g\n", double(o.ev()));
        // The synthetic scenes in sequence (ten frames each) at 16 ms.
        ExposureState z(p);
        std::printf("simulate_scenes");
        for (const auto& sc : scenes()) {
            const MeterStatistics m = statistics(sc, weights.data(), p);
            for (unsigned f = 0; f < 10; ++f) {
                std::printf(" %.9g", double(z.ev()));
                z.step(m, 0.016f);
            }
        }
        std::printf(" %.9g\n", double(z.ev()));
    }
    // Weighting round trip.
    float rgb[3] = {2.f, 40.f, .5f};
    weight_color(rgb, 1.5f);
    std::printf("weighted %.9g %.9g %.9g\n", double(rgb[0]), double(rgb[1]), double(rgb[2]));
    unweight_color(rgb, 1.5f);
    std::printf("unweighted %.9g %.9g %.9g %.9g\n", double(rgb[0]), double(rgb[1]), double(rgb[2]),
                double(luma_weight(.5f, 2.f)));
    return 0;
}
