// Review of the arrival round item 4 (fail-before): the lone re-departure's decision and the gate-mouth block read the same predecessor,
// on gc::lane_frame alone, compiled against a given src/proxy (item4_before.sh: 97f3804b and the working tree). A re-departing member at
// the gate mouth (1.5 km out, 600 m above Lambda, its nose away from the gate); its predecessor is the member at the hand-over 1 km out
// (nearer the gate: no hold); a third member on Lambda 30 km out heading in, ranked behind it, would hold it. Before: the decision
// tested every member (held by the third: deferred for good) while the block tested the predecessor (no hold): undecided and not held.
#include "ship_gate_ctl_core.h"
#include <cstdio>
namespace gc = x3m::ship_avoidance::ctl;
namespace core = x3m::ship_steering::core;
int main() {
    static gc::Board b;
    b = gc::Board{};
    b.lane = 1;
    const float G[3] = {0, 0, 0}, R[3] = {1, 0, 0}, U[3] = {0, 1, 0}, F[3] = {0, 0, 1};
    gc::Gate* g = gc::acquire_gate(b, 0x4000, 1000, G, R, U, F, 1105.0f, 0);
    auto mk = [&](unsigned i, std::uint32_t ship, float y, float z, float nose) {
        gc::Member& m = g->m[i];
        m.ship = m.id = ship;
        m.role = gc::Role::committed;
        m.seq = i + 1;
        m.r = 1000.0f;
        m.w = 530.0f;
        m.L = 1000.0f;
        m.params.v_max = 110.0f;
        m.params.W = 0.1f;
        m.params.alpha = 0.05f;
        m.params.a_lin = 5.0f;
        m.large = 1;
        m.proven = 1;
        m.line_ok = 1;
        m.st.order = core::kOrderFlyBefore;
        m.st.target = 0x4000u;
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
    mk(0, 0x9000, -929.0f, -1000.0f, 1.0f);   // at the hand-over, 1 km out
    mk(1, 0x9100, 600.0f, -1500.0f, -1.0f);   // re-departing at the mouth
    mk(2, 0x9200, -929.0f, -30000.0f, 1.0f);  // on Lambda 30 km out, heading in
    unsigned undecided_free = 0, frames = 0;
    for (std::uint32_t t = 1000; t <= 11000; t += 100) {
        g->step_ms = 100;
        for (unsigned i = 0; i < g->n; ++i) g->m[i].st.ms = g->m[i].live_ms = g->m[i].decided_ms = g->m[i].commit_ms = g->m[i].steered_ms = t;
        g->m[0].sb = gc::Sb::handover;
        g->m[0].handover_ms = 1000;
        gc::LaneStats ls;
        gc::lane_frame(*g, t, &ls);
        const gc::Member& m = g->m[1];
        ++frames;
        if (t > 1500 && m.redep && !m.loop_min_done && !(m.law == gc::Law::stop && m.v_sb == 0.0f)) ++undecided_free;
    }
    const gc::Member& m = g->m[1];
    std::printf("redep %u rank %u pred 0x%x loop %u loop_min %u decided %u law %s: %u of %u frames undecided and not held (want 0)\n",
                unsigned(m.redep), unsigned(m.lane_rank), m.lane_pred, unsigned(m.loop), unsigned(m.loop_min), unsigned(m.loop_min_done),
                gc::law_name(m.law), undecided_free, frames);
    return 0;
}
