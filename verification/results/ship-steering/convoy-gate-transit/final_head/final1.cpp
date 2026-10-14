// Final review item 1: the previous lane order C, B, A with A on Lambda 300 m ahead of C and B joining between them by ETA (inside the
// 5 s hysteresis of both), and 400 random boards (random previous order): on-lane pairs ranked against their positions.
#include <initializer_list>
#include <cstdio>
#include <cmath>
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
namespace core = x3m::ship_steering::core;
static gc::Board b{};
static gc::Gate* g = nullptr;
static void mk(unsigned i, unsigned ship, float z, float x) {
    gc::Member& m = g->m[i]; m = gc::Member{}; m.ship = m.id = ship; m.role = gc::Role::committed; m.seq = i + 1; m.r = 1000; m.w = 530; m.L = 1000;
    m.params.v_max = 110; m.params.W = 0.1f; m.params.alpha = 0.05f; m.params.a_lin = 5; m.large = 1; m.live_ms = m.decided_ms = m.commit_ms = m.steered_ms = 1000;
    m.proven = 1; m.line_ok = 1; m.st.ms = 1000; m.st.order = core::kOrderFlyBefore; m.st.target = 0x4000; m.st.x[0] = x; m.st.x[1] = -929; m.st.x[2] = z;
    m.st.rows[2][2] = 1; m.st.f_ok = 1; m.st.v = 100; m.st.u[2] = 100;
    if (x != 0) { m.st.rows[2][0] = x > 0 ? -0.26f : 0.26f; m.st.rows[2][2] = 0.966f; }
    if (i + 1 > g->n) g->n = i + 1;
}
static void fresh() {
    b = gc::Board{}; b.lane = 1;
    const float G[3] = {0, 0, 0}, R[3] = {1, 0, 0}, U[3] = {0, 1, 0}, F[3] = {0, 0, 1};
    g = gc::acquire_gate(b, 0x4000, 1000, G, R, U, F, 1105.0f, 0);
}
int main() {
    gc::LaneStats ls;
    fresh();
    mk(0, 0x9000, -10000, 0); mk(1, 0x9100, -9891, 2000); mk(2, 0x9200, -10300, 0);
    gc::lane_frame(*g, 1000, &ls);
    g->lane_ord[0] = 0x9200; g->lane_ord[1] = 0x9100; g->lane_ord[2] = 0x9000; g->lane_n = 3;
    gc::lane_frame(*g, 1100, &ls);
    std::printf("C,B,A: A rank %u law %s v %.1f | B rank %u | C rank %u behind %x\n", g->m[0].lane_rank, gc::law_name(g->m[0].law), double(g->m[0].v_sb),
                g->m[1].lane_rank, g->m[2].lane_rank, g->m[2].lane_pred);
    unsigned rs = 0x2545f491u, bad_pos = 0, bad_rank = 0;
    auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return double(rs >> 8) / double(1u << 24); };
    for (int t = 0; t < 400; ++t) {
        fresh();
        const unsigned non = 4 + unsigned(rnd() * 4.0);
        for (unsigned i = 0; i < 8; ++i) {
            const float z = -9000.0f - float(rnd() * 9000.0);
            const float x = i < non ? 0.0f : float((rnd() < 0.5 ? -1.0 : 1.0) * (500.0 + rnd() * 2500.0));
            mk(i, 0x9000 + 0x100 * i, z, x);
        }
        gc::lane_frame(*g, 1000, &ls);
        unsigned perm[8];
        for (unsigned i = 0; i < 8; ++i) perm[i] = i;
        for (unsigned i = 7; i > 0; --i) { const unsigned j = unsigned(rnd() * double(i + 1)) % (i + 1); const unsigned q = perm[i]; perm[i] = perm[j]; perm[j] = q; }
        for (unsigned i = 0; i < 8; ++i) g->lane_ord[i] = g->m[perm[i]].ship;
        g->lane_n = 8;
        gc::lane_frame(*g, 1100, &ls);
        unsigned seen = 0;
        for (unsigned i = 0; i < 8; ++i) if (g->m[i].lane_rank < 8) seen |= 1u << g->m[i].lane_rank;
        if (seen != 0xffu) ++bad_rank;
        for (unsigned i = 0; i < 8; ++i)
            for (unsigned j = 0; j < 8; ++j)
                if (g->m[i].lane_on && g->m[j].lane_on && g->m[i].st.x[2] > g->m[j].st.x[2] && g->m[i].lane_rank > g->m[j].lane_rank) ++bad_pos;
    }
    std::printf("400 random boards: %u with a rank missing, %u on-lane pairs against their positions\n", bad_rank, bad_pos);
}
