// Amendment 16: the frame-end predictor's worst evaluation over window states, amendment 15's join_choose against join_evaluate16
// (from join mode, and from the pred_none fallback, which computes up to all four candidates for the un-latch and the slow-join test),
// the review sweep of capital-gate-transit/pred_cost.cpp (s -6..-0.1 r, lateral 100 m..20 km, nose -180..165 deg, the Run156 M2
// caps; host timing, the best of 7 runs per state; with an argument N the whole sweep runs N times and each state keeps its best
// over all sweeps: a host preemption that inflates all 7 back-to-back runs of one state does not survive another sweep, amendment
// 16b's review of the 101 us outlier). Usage (from the repository root):
//   c++ -std=c++17 -O2 -I src/proxy verification/results/ship-steering/capital-crowd/pred_cost16.cpp -o /tmp/pred_cost16 && /tmp/pred_cost16 [N]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "ship_steering_core.h"
namespace core = x3m::ship_steering::core;
static volatile float g_sink;
int main(int argc, char** argv) {
    const int sweeps = argc > 1 ? std::atoi(argv[1]) : 1;
    std::vector<double> best;
    core::JoinParams p;
    p.W = float(1843.0 / 65536.0 * 2 * M_PI);
    p.alpha = p.W * 0.05f;
    p.v_max = 110;
    p.a_lin = 10;
    p.r_gate = 1105;
    double worst[3] = {0, 0, 0};
    float where[3][3] = {};
    unsigned states = 0;
    for (int sw = 0; sw < (sweeps > 0 ? sweeps : 1); ++sw) {
    unsigned idx = 0;
    for (double s = -6.0; s <= -0.1; s += 0.5)
        for (double y = 100; y <= 20000; y *= 1.5)
            for (double psi = -180; psi < 180; psi += 15) {
                if (sw == 0) ++states;
                core::JoinPlane q;
                q.s = float(s * 1105);
                q.y = float(y);
                q.psi = q.upsi = float(psi * M_PI / 180);
                q.w = 0;
                q.v = 100;
                for (int which = 0; which < 3; ++which) {
                    double us = 1e30;
                    for (int r = 0; r < 7; ++r) {
                        core::JoinState j;
                        j.mode = which == 2 ? core::JoinMode::fallback : core::JoinMode::join;
                        j.reason = which == 2 ? core::JoinReason::pred_none : core::JoinReason::none;
                        j.plane = q;
                        j.params = p;
                        j.plane_ms = 1000;
                        core::JoinEval ev;
                        float v = 0, n = 0, ye = 0;
                        unsigned preds = 0;
                        const auto t0 = std::chrono::steady_clock::now();
                        if (which == 0) {
                            core::join_choose(q, p, 100.0f, &v, &n, &ye, &preds);
                            g_sink = v + n + ye;
                        }
                        else {
                            core::join_evaluate16(j, 1000, 250, &ev);
                            g_sink = j.v_cmd + ev.cand[3].nose;
                        }
                        const double u = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                        if (u < us) us = u;
                    }
                    if (sw == 0) best.push_back(us);
                    else if (us < best[idx]) best[idx] = us;
                    us = best[idx++];
                    if (sw + 1 == (sweeps > 0 ? sweeps : 1) && us > worst[which]) {
                        worst[which] = us;
                        where[which][0] = float(s);
                        where[which][1] = float(y);
                        where[which][2] = float(psi);
                    }
                }
            }
    }
    static const char* const names[3] = {"join_choose (amendment 15)", "join_evaluate16 from join", "join_evaluate16 from the fallback"};
    std::printf("%u window states, %d sweep(s)\n", states, sweeps > 0 ? sweeps : 1);
    for (int i = 0; i < 3; ++i)
        std::printf("worst %-34s %.1f us at s=%.1f r y=%.0f m psi=%.0f deg\n", names[i], worst[i], double(where[i][0]), double(where[i][1]), double(where[i][2]));
}
