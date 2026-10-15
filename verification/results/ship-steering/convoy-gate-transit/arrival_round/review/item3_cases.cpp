// Review of the arrival round item 3 (fail-before): the gate-mouth hold's game-time bound on gc::lane_frame alone, compiled against a given
// src/proxy (item3_before.sh: 97f3804b and the working tree). A re-departing escort at the gate mouth (1.5 km out, 600 m above Lambda,
// its nose away from the gate) behind its leader on Lambda 6 km out heading in at 100 m/s; the leader never launches: (stuck) its state
// stays there (its launch ETA never advances: not a physical stop, which is the queue hold's block and lasts the leader's membership),
// (killed) it leaves the board at 30 s. Steps 100 ms (tf 1) and 1.5 s (a SETA step). Prints the escort's last held frame (the law's stop
// at v 0 while re-departing at the mouth) in game seconds; the bound is the leader's launch ETA at the hold's start (37 s to d_end at the
// lane speed + 1 s + the order-5 leg) + T_stop (20 s) + the step.
#include "ship_gate_ctl_core.h"
#include <cstdio>
#include <initializer_list>
namespace gc = x3m::ship_avoidance::ctl;
namespace core = x3m::ship_steering::core;
static double run(bool kill, std::uint32_t step) {
    static gc::Board b;
    b = gc::Board{};
    b.lane = 1;
    const float G[3] = {0, 0, 0}, R[3] = {1, 0, 0}, U[3] = {0, 1, 0}, F[3] = {0, 0, 1};
    gc::Gate* g = gc::acquire_gate(b, 0x4000, 1000, G, R, U, F, 1105.0f, 0);
    auto mk = [&](unsigned i, std::uint32_t ship, gc::Role role, float y, float z, float nose, std::uint32_t leader) {
        gc::Member& m = g->m[i];
        m.ship = m.id = ship;
        m.role = role;
        m.seq = i + 1;
        m.r = 1000.0f;
        m.w = 530.0f;
        m.L = 1000.0f;
        m.params.v_max = 110.0f;
        m.params.W = 0.1f;
        m.params.alpha = 0.05f;
        m.params.a_lin = 5.0f;
        m.large = 1;
        m.leader = leader;
        m.live_ms = m.decided_ms = m.commit_ms = m.steered_ms = 1000;
        m.proven = 1;
        m.line_ok = 1;
        m.st.ms = 1000;
        m.st.order = role == gc::Role::escort ? core::kOrderEscort : core::kOrderFlyBefore;
        m.st.target = role == gc::Role::escort ? leader : 0x4000u;
        m.st.x[0] = 0.0f;
        m.st.x[1] = y;
        m.st.x[2] = z;
        m.st.rows[2][2] = nose;
        m.st.f_ok = 1;
        if (nose > 0.0f) {
            m.st.v = 100.0f;
            m.st.u[2] = 100.0f;
        }
        if (i + 1 > g->n) g->n = i + 1;
    };
    mk(0, 0x9000, gc::Role::committed, -929.0f, -6000.0f, 1.0f, 0);
    mk(1, 0x9100, gc::Role::escort, 600.0f, -1500.0f, -1.0f, 0x9000);
    double last = -1.0;
    gc::LaneStats ls;
    for (std::uint32_t t = 1000; t <= 240000; t += step) {
        g->step_ms = step;
        for (unsigned i = 0; i < g->n; ++i) g->m[i].st.ms = g->m[i].live_ms = g->m[i].decided_ms = g->m[i].commit_ms = g->m[i].steered_ms = t;
        if (kill && t >= 30000) g->m[0].role = gc::Role::none; // (destroyed: no longer a member)
        gc::lane_frame(*g, t, &ls);
        const gc::Member& m = g->m[1];
        if (m.redep && m.law == gc::Law::stop && m.v_sb == 0.0f) last = double(t) / 1000.0;
    }
    return last;
}
int main() {
    for (std::uint32_t step : {100u, 1500u})
        std::printf("step %4u ms: stuck leader: escort held until %.1f s; killed at 30 s: held until %.1f s\n", step, run(false, step), run(true, step));
    return 0;
}
