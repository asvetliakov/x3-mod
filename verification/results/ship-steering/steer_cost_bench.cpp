// Host micro-benchmark: core::steer() and av::resolve() (420 obstacles) per call, old (af5ec4cf) vs new headers.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "ship_avoidance_core.h"
namespace core = x3m::ship_steering::core;
namespace av = x3m::ship_avoidance::core;
static av::Snapshot snap;
int main() {
    core::Dynamics dyn;
    dyn.W[0] = dyn.W[1] = 1.6f;
    dyn.W[2] = 1.2f;
    dyn.k = 0.5f;
    dyn.v_max = 250;
    dyn.a_lin = 30;
    dyn.r_own = 12;
    std::uint32_t seed = 1;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.0f; };
    const int N = 2000000;
    double sink = 0;
    core::Inputs in;
    in.row[0][0] = in.row[1][1] = in.row[2][2] = 1;
    in.order = 4;
    in.v = 200;
    in.arrival_ramp = true;
    in.d_arr = 50;
#ifdef NEWAPI
    in.slip[0] = 30; in.slip[1] = -5; in.slip[2] = 2;
#endif
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        in.P[0] = 2000 * (rnd() - 0.5f);
        in.P[1] = 500 * (rnd() - 0.5f);
        in.P[2] = 5000 * rnd();
        core::Command c;
        core::steer(in, dyn, &c);
        sink += c.v_cmd + c.e[0];
    }
    auto t1 = std::chrono::steady_clock::now();
    std::printf("steer ns/call %.1f\n", std::chrono::duration<double, std::nano>(t1 - t0).count() / N);
    // resolve: 420 ships around, 3 in conflict-ish range
    snap.sector = 1;
    snap.count = 420;
    for (unsigned i = 0; i < 420; ++i) {
        av::Obstacle& o = snap.items[i];
        o = av::Obstacle{};
        for (int k = 0; k < 3; ++k) o.x[k] = 20000 * (rnd() - 0.5f);
        if (i < 3) { o.x[0] = 30.0f * i; o.x[2] = 1500 + 400 * i; }
        for (int k = 0; k < 3; ++k) o.v[k] = 100 * (rnd() - 0.5f);
        o.r = 20;
        o.ptr = 0x10000 + i * 16;
        o.kind = 0;
    }
    snap.max_r = 20;
    av::Self s;
    s.x[0] = s.x[1] = s.x[2] = 0;
    s.f[0] = s.f[1] = 0; s.f[2] = 1;
#ifdef NEWAPI
    s.slip[0] = 30; s.slip[1] = -5; s.slip[2] = 2;
#endif
    s.v = 200; s.r = 12; s.a = 30; s.omega = 1.6f; s.alpha = 0.8f;
    s.up[0] = 0; s.up[1] = 1; s.up[2] = 0;
    s.ptr = 1;
    float P[3] = {0, 0, 20000};
    av::ShipAvoid st{};
    const int M = 200000;
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < M; ++i) {
        const av::Result r = av::resolve(snap, nullptr, 0, s, P, st, std::uint32_t(i * 50));
        sink += r.P[0];
    }
    t1 = std::chrono::steady_clock::now();
    std::printf("resolve(420) ns/call %.1f\n", std::chrono::duration<double, std::nano>(t1 - t0).count() / M);
    std::fprintf(stderr, "%g\n", sink);
    return 0;
}
