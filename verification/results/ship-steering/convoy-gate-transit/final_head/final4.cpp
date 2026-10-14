// Final review item 4: an escort's L_i (on l_c 929 m below the axis) read back after a load with no board: lane_point_own (HEAD's
// adoption) and, in the tree, the l_c envelope.
#include <initializer_list>
#include <cstdio>
#include <cmath>
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
int main() {
    const float G[3] = {0, 0, 0}, R[3] = {1, 0, 0}, U[3] = {0, 1, 0}, F[3] = {0, 0, 1}, r_gate = 1105.0f, r_s = 760.0f;
    gc::Gate g{}; g.r_gate = r_gate; g.F[2] = 1; g.R[0] = 1; g.U[1] = 1; g.lane_c[1] = -929; g.lane_c_ok = 1;
    gc::Member e{}; e.r = r_s; e.w = 0.53f * r_s; e.L = r_s; e.params.v_max = 110; e.lane_esc = 1;
    e.q_ok = gc::lane_q(e.r, e.w, e.L, e.params.v_max, r_gate, false, e.q) ? 1 : 0;
    float Le[3];
    gc::lane_order_point(g, e, Le);
    const bool own = gc::lane_point_own(G, R, U, F, r_gate, e.r, e.w, e.L, e.params.v_max, false, nullptr, Le);
#ifdef X3M_HEAD
    std::printf("escort L_i (l_c -929) after a load, no board: adopted %d\n", int(own));
#else
    std::printf("escort L_i (l_c -929) after a load, no board: adopted %d\n", int(own || gc::lane_point_lc_env(G, R, U, F, r_gate, Le)));
#endif
}
