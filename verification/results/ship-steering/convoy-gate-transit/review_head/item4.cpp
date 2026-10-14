#include <initializer_list>
#include <cstdio>
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
// Review item 4: an escort's L_i (on l_c) read back after l_c moved 15 m (3 s at the 5 m/s shift rate) is still recognised as ours.
int main() {
    gc::Gate g{};
    const float G[3] = {100, -200, 300};
    for (int i = 0; i < 3; ++i) { g.G[i] = G[i]; g.R[i] = i == 0; g.U[i] = i == 1; g.F[i] = i == 2; }
    g.r_gate = 1105.0f;
    g.lane_c[0] = 0.0f; g.lane_c[1] = -929.0f; g.lane_c_ok = 1;
    gc::Member m{};
    m.r = 1092.0f; m.w = 0.53f * 1092.0f; m.L = 1092.0f; m.params.v_max = 110.0f; m.lane_esc = 1;
    m.q_ok = gc::lane_q(m.r, m.w, m.L, m.params.v_max, g.r_gate, false, m.q) ? 1 : 0;
    float L[3];
    gc::lane_order_point(g, m, L);
    g.lane_c[1] = -944.0f;
    std::printf("escort L_i after a 15 m l_c drift: ours %d\n", int(gc::lane_point_ours(g, m, L)));
}
