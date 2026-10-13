// B.4 cost: site E's pure decision (av::site_e_verdict) per call, the base (a2d7058f, namespace sa, extracted as keyoff_identity_b.sh
// does) against the working tree, on 4,096 inputs drawn like keyoff_identity_b.cpp's high-bit sweep with the capital key's
// small_any_age = small_dock_legs on the tree side; three alternating runs of 200 passes, best per run. Game-memory reads (the DLL's
// e_verdict field reads) are not included. Build: c++ -std=c++17 -O2 -I src/proxy -I <dir with sa/> e_cost_b4.cpp
#include "ship_avoidance_core.h"
#include "sa/ship_avoidance_core.h"
#include <chrono>
#include <cstdio>

namespace av = x3m::ship_avoidance::core;
namespace sa_av = sa::x3m::ship_avoidance::core;

struct Mix {
    std::uint64_t s;
    unsigned pick(unsigned k) { return unsigned((s = s * 6364136223846793005ull + 1442695040888963407ull) >> 33) % k; }
};
constexpr unsigned kN = 4096;
static av::SiteE g_in[kN];
static sa_av::SiteE g_o[kN];

int main() {
    Mix rng{20261014u};
    const std::uint16_t orders[9] = {0, 4, 5, 0x12, 0x14, 0x15, 0x1f, 0x1e, 0x1d};
    const std::uint32_t ages[6] = {0, 100, 240, 340, 2000, 2900};
    unsigned small_stale = 0;
    for (unsigned k = 0; k < kN; ++k) {
        av::SiteE& in = g_in[k];
        sa_av::SiteE& o = g_o[k];
#define SET(f, v) \
    do { \
        const auto val_ = (v); \
        in.f = val_; \
        o.f = val_; \
    } while (0)
        SET(a_order, orders[rng.pick(9)]);
        SET(a_in_table, rng.pick(8) != 0);
        SET(a_age_ms, ages[rng.pick(6)]);
        SET(frame_ms, rng.pick(2) ? 16u : 70u);
        SET(a_capital, rng.pick(4) != 0);
        SET(r_a, rng.pick(2) ? 1091.5f : 60.0f);
        SET(b_gate, rng.pick(6) == 0);
        SET(gates_live, true);
        SET(capital_policy, true);
        SET(gate_state, rng.pick(2) != 0);
        SET(b_ship, !in.b_gate);
        SET(b_readable, true);
        SET(b_capital_code, rng.pick(3) == 0);
        SET(r_b, rng.pick(2) ? 86.5f : 1022.2f);
        SET(b_flags, 0x80000u);
        SET(b_order, orders[rng.pick(9)]);
        SET(small_dock_legs, true);
        SET(avoidance_on, true);
        SET(b_resolved, rng.pick(3) == 0);
#undef SET
        in.small_any_age = true;
        small_stale += in.b_ship && in.a_capital && !in.b_capital_code && in.r_b < in.r_a / 3 && in.a_age_ms > 250;
    }
    double best[3][2];
    volatile unsigned sink = 0;
    for (unsigned run = 0; run < 3; ++run)
        for (unsigned side = 0; side < 2; ++side) { // alternating: base, tree, base, tree, ...
            double b = 1e30;
            for (unsigned pass = 0; pass < 200; ++pass) {
                const auto t0 = std::chrono::steady_clock::now();
                unsigned acc = 0;
                if (side == 0)
                    for (unsigned k = 0; k < kN; ++k) acc += unsigned(sa_av::site_e_verdict(g_o[k]));
                else
                    for (unsigned k = 0; k < kN; ++k) acc += unsigned(av::site_e_verdict(g_in[k]));
                const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / kN;
                sink = sink + acc;
                if (ns < b) b = ns;
            }
            best[run][side] = b;
        }
    std::printf("site_e_verdict per call (host -O2, %u inputs, %u small pairs with a stale A), best of 200 passes:\n", kN, small_stale);
    for (unsigned run = 0; run < 3; ++run) std::printf("  run %u: base %.2f ns, B.4 %.2f ns\n", run + 1, best[run][0], best[run][1]);
    return sink == 0xffffffffu ? 1 : 0;
}
