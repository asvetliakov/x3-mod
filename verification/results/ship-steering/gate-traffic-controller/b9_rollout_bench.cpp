// B.9 bench (b9_rollout_bench.txt): one petal rollout of an inbound member (the escort crossing toward its petal from the side, inside the inbound law's
// reach), of a ringed holder without a target, and (B.9 review) of holders with a lap target (ringed; unringed and late), old core (-I oldcore)
// against the new (-I worktree src/proxy). Prints ns per rollout (best of 7 x 20000).
#include "ship_gate_ctl_core.h"
#include <chrono>
#include <cstdio>
namespace gc = x3m::ship_avoidance::ctl;
static double bench(gc::Member& m, const gc::Gate& g) {
    gc::Sample out[gc::kSamples];
    double best = 1e30;
    volatile float sink = 0;
    for (int rep = 0; rep < 7; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20000; ++k) {
            float vmax = 0;
            m.st.x[0] += 1e-3f;
            const unsigned n = gc::rollout(m, g, -1.0f, out, &vmax);
            sink = sink + out[n - 1].c[0];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 20000.0;
        if (ns < best) best = ns;
    }
    return best;
}
int main() {
    static gc::Gate g;
    g.gate = 0x1000;
    g.r_gate = 1105.0f;
    g.rho = 2700.0f;
    static gc::Member m;
    m.ship = 0x9100;
    m.role = gc::Role::escort;
    m.leader = 0x9000;
    m.sb = gc::Sb::inbound;
    m.petal = 3;
    m.zx = -6000.0f;
    m.params.W = 1843.0f / 65536.0f * 6.2831853f;
    m.params.alpha = m.params.W * 0.05f;
    m.params.v_max = 110.0f;
    m.params.a_lin = 10.0f;
    m.params.r_gate = 1105.0f;
    m.st.ms = 100000;
    m.st.order = 0x14;
    m.st.target = 0x9000;
    const float x[3] = {-6000.0f, -2300.0f, -13600.0f}, f[3] = {0.95f, 0.0f, 0.312f};
    for (int i = 0; i < 3; ++i) {
        m.st.x[i] = x[i];
        m.st.rows[2][i] = f[i];
        m.st.u[i] = 100.0f * f[i];
    }
    m.st.rows[0][0] = 0.312f; m.st.rows[0][1] = 0; m.st.rows[0][2] = -0.95f;
    m.st.rows[1][0] = 0; m.st.rows[1][1] = 1; m.st.rows[1][2] = 0;
#ifndef BENCH_V // the plan speed (-DBENCH_V=40: the holders fly the same speed in both cores, the hold law's own cost apart from the path)
#define BENCH_V 110.0f
#endif
    m.st.v = m.st.v_des = BENCH_V < 100.0f ? BENCH_V : 100.0f;
    m.v_plan = BENCH_V;
    m.v_plan_ms = 100000;
    const double inb = bench(m, g);
    m.sb = gc::Sb::hold;
    m.ringed = 1;
    const double hold = bench(m, g);
    // B.9 review: holders with a lap target (the rollout evaluates hold_law once per sample): ringed, target 60 s ahead (the lap law);
    // unringed and late (B.8's join profile)
    m.t_rel_ms = m.st.ms + 60000;
    const double hold_t = bench(m, g);
    m.ringed = 0;
    m.t_rel_ms = m.st.ms;
    const double late = bench(m, g);
    std::printf("petal rollout: inbound (crossing, in the law's reach) %.0f ns, ringed holder %.0f ns; with a target: ringed %.0f ns, "
                "unringed late %.0f ns\n", inb, hold, hold_t, late);
#ifdef HOLD_LAW // the new core: hold_law alone (the rollout calls it once per sample of a holder), states spread over the petal's region
    {
        gc::PetalGeom pg;
        gc::petal_geom(g, 3u, m.zx, &pg);
        const float open = gc::hold_open(m, pg);
        volatile float hsink = 0;
        double hbest = 1e30;
        for (int rep = 0; rep < 7; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < 1000000; ++k) {
                gc::HoldState h;
                h.a = -9000.0f + float(k % 4000);
                h.b = 2000.0f + float(k % 1500);
                h.o = float(k % 700);
                h.f_F = 0.8f;
                h.ringed = (k & 1) != 0;
                hsink = hsink + gc::hold_law(m, pg, open, false, h, 1000u, 60000 - (k % 90000));
            }
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 1e6;
            if (ns < hbest) hbest = ns;
        }
        std::printf("hold_law: %.1f ns per call\n", hbest);
    }
#endif
    return 0;
}
