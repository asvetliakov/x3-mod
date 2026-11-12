// FL 70 A (2026-11-13): the per-cell fade filter's CPU cost in fill_region, same session A/B. Compiled twice by
// fade_filter_bench.py: once against the base tree (84a2ccaaa, namespace renamed by -Dx3m=x3m_base, BENCH_NAME
// bench_base) and once against this tree (bench_new); the driver TU (BENCH_MAIN) times both on one workload: sixteen
// capital owners x six faces, sixteen hits each spread over 600 m and aged 0..2.9 s (the fixture's sixteen-capitals
// shape), 200 repetitions, best and mean per call of the 96 regions.
#ifndef BENCH_MAIN
#include "proxy/weapon_effects_stage.h"
#include <chrono>
namespace ws = x3m::weapon_effects::stage;
namespace wc = x3m::weapon_effects::core;
namespace rr = x3m::renderer;
void BENCH_NAME(unsigned reps, double* best, double* mean, unsigned* cells) {
    static rr::WeaponPatch out[64];
    const ws::Tuning t{};
    const float frame[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}, centre[3] = {0.f, 0.f, 4000.f};
    const float colour[3] = {0.5f, 0.6f, 1.f};
    float points[16][3], ages[16];
    for (unsigned h = 0; h < 16; ++h) {
        float a = 0.f, b = 0.f, f = 1.f;
        for (unsigned i = h + 1; i; i /= 2) f *= 0.5f, a += f * float(i % 2);
        f = 1.f;
        for (unsigned i = h + 1; i; i /= 3) f /= 3.f, b += f * float(i % 3);
        points[h][0] = ws::metres_to_view(600.f * (a - 0.5f));
        points[h][1] = ws::metres_to_view(600.f * (b - 0.5f));
        points[h][2] = ws::metres_to_view(300.f);
        ages[h] = 2.9f * (float(h) + 0.5f) / 16.f; // newest first
    }
    *best = 1e30;
    *mean = 0.0;
    *cells = 0;
    for (unsigned r = 0; r < reps; ++r) {
        unsigned c = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (unsigned owner = 0; owner < 16; ++owner)
            for (unsigned face = 0; face < 6; ++face) {
                ws::RegionStats rs{};
                ws::fill_region(out, 8, centre, frame, wc::patch_capital, face, points, ages, 16, colour, t, &rs);
                c += rs.cells;
            }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        *best = us < *best ? us : *best;
        *mean += us / reps;
        *cells = c;
    }
}
#else
#include <cstdio>
void bench_base(unsigned, double*, double*, unsigned*);
void bench_new(unsigned, double*, double*, unsigned*);
int main() {
    for (unsigned round = 0; round < 3; ++round) { // alternate, so a host-load drift hits both
        double bb, bm, nb, nm;
        unsigned bc, nc;
        bench_base(200, &bb, &bm, &bc);
        bench_new(200, &nb, &nm, &nc);
        std::printf("round=%u base_best_us=%.1f base_mean_us=%.1f base_cells=%u new_best_us=%.1f new_mean_us=%.1f "
                    "new_cells=%u\n", round, bb, bm, bc, nb, nm, nc);
    }
    return 0;
}
#endif
