// The amendment-15 predictor's worst evaluation over window states (review sweep, host timing; the best of 7 runs per state):
// s -6..-0.1 r, lateral 100 m..20 km, nose -180..165 deg, the Run156 M2 caps. Usage (from the repository root):
//   c++ -std=c++17 -O2 -I src/proxy verification/results/ship-steering/capital-gate-transit/pred_cost.cpp -o /tmp/pred_cost && /tmp/pred_cost
// 4096 steps per candidate: 155.9 us at s -1.0 r, 19.5 km, 105 deg; 1024 steps: 80.8-81.3 us at s -3.5 r, 19.5 km, 105 deg.
#include <chrono>
#include <cmath>
#include <cstdio>
#include "ship_steering_core.h"
namespace core = x3m::ship_steering::core;
int main() {
    core::JoinParams p; p.W = float(1843.0 / 65536.0 * 2 * M_PI); p.alpha = p.W * 0.05f; p.v_max = 110; p.a_lin = 10; p.r_gate = 1105;
    double worst = 0; float ws=0, wy=0, wpsi=0;
    for (double s = -6.0; s <= -0.1; s += 0.5)
      for (double y = 100; y <= 20000; y *= 1.5)
        for (double psi = -180; psi < 180; psi += 15) {
            core::JoinPlane q; q.s = float(s * 1105); q.y = float(y); q.psi = q.upsi = float(psi * M_PI / 180); q.w = 0; q.v = 100;
            float v, n, ye; unsigned preds = 0;
            double us = 1e30;
            for (int r = 0; r < 7; ++r) { auto t0 = std::chrono::steady_clock::now(); core::join_choose(q, p, 100.0f, &v, &n, &ye, &preds);
              double u = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(); if (u < us) us = u; }
            if (us > worst) { worst = us; ws = float(s); wy = float(y); wpsi = float(psi); }
        }
    std::printf("worst join_choose in window: %.1f us at s=%.1f r y=%.0f m psi=%.0f deg\n", worst, ws, wy, wpsi);
}
