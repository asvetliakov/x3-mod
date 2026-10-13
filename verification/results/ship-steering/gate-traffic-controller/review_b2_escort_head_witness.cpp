// Review witness (B.2, 2026-10-13): a reserved escort heads the sequence while its leader holds, so nothing is released. Members that
// entered within kBurstMs (a load) are ordered by enter_d (seq_before), escorts by key: with the leader L (key 16) entered before a
// committed holder N (key 32) that is nearer X, the scan picks N over L and then L's escort E (key 17) over N. The B.2 guard excludes an
// escort only while its leader is inbound or keyed behind it, so E heads, the committed branch never runs and no head skip applies.
// Build and run from the repository root:
//   c++ -std=c++17 -O2 -w verification/results/ship-steering/gate-traffic-controller/review_b2_escort_head_witness.cpp -o /tmp/w && /tmp/w
#define main fixture_main
#include "../../../probe/ship_steering_host.cpp"
#undef main
static gc::Member* put_titan(gc::Board& bd, gc::Gate& g, unsigned k, int slot, gc::Role role, std::uint16_t order, const float off[3], float W, std::uint32_t now) {
    double A[3]; float Af[3]; core::GateGeometry geo; bool valid = false;
    capital_grid(kCrowdTitan, slot, kGateIdentity, 0.0, A, Af, &geo, &valid);
    const float x[3] = {geo.X[0] + off[0], geo.X[1] + off[1], geo.X[2] + off[2]}, F[3] = {0, 0, 1};
    gc::Member* m = ctl_put(bd, g, 0x9000 + 0x100 * k, role, order, x, F, 40.0f, 1092, 583, now);
    if (!m) return nullptr;
    for (int i = 0; i < 3; ++i) { m->X[i] = geo.X[i]; m->h[i] = geo.h[i]; }
    m->line_ok = role == gc::Role::committed ? 1 : 0;
    m->params.W = W; m->params.alpha = W * 0.05f; m->params.a_lin = 10.0f; m->params.v_max = 110.0f;
    return m;
}
int main() {
    for (int inverted = 0; inverted < 2; ++inverted) {
        static gc::Board bd; bd = gc::Board{};
        std::uint32_t now = 400000;
        gc::Gate* g = ctl_gate(bd, now);
        const float offN[3] = {2000.0f, -2500.0f, -4000.0f}, offL[3] = {0.0f, -2500.0f, -4000.0f}, offE[3] = {-2500.0f, -1500.0f, -6000.0f};
        // slots: N (another committed holder) first, then the leader L, then L's escort E: all entered within kBurstMs (a load)
        gc::Member* L = put_titan(bd, *g, 0, 0, gc::Role::committed, core::kOrderFlyBefore, offL, 0.157f, now);
        gc::Member* N = put_titan(bd, *g, 3, 3, gc::Role::committed, core::kOrderFlyBefore, offN, 0.157f, now);
        gc::Member* E = put_titan(bd, *g, 1, 1, gc::Role::escort, core::kOrderEscort, offE, 0.157f, now);
        E->leader = L->ship; E->st.target = L->ship;
        // keys as after entry: L entered first (seq 1 -> key 16), N second (key 32); N nearer X (burst rule puts N before L)
        /* natural keys: seq << 4 at the first sb_frame */ g->m[0].key = 0;
        L->enter_ms = N->enter_ms = now; { gc::SbStats s0; gc::sb_frame(*g, now - 100, &s0); g->petal_ship[0]=g->petal_ship[0]; } L->enter_d = 5000.0f; N->enter_d = inverted ? 1000.0f : 9000.0f; L->sb = N->sb = gc::Sb::hold; L->release_ms = N->release_ms = 0; g->funnel_ship = 0;
        unsigned rel = 0, esc_head = 0; int first_rel = -1;
        for (int it = 0; it < 3000; ++it, now += 100) { // 300 s
            for (unsigned k = 0; k < g->n; ++k) { g->m[k].st.ms = now; if (g->m[k].role == gc::Role::committed) g->m[k].commit_ms = now; }
            gc::SbStats st; gc::sb_frame(*g, now, &st);
            rel += st.releases;
            if (st.releases && first_rel < 0) first_rel = it;
            if (st.seq.cand == E->ship) ++esc_head;
        }
        std::printf("inverted=%d (N enter_d %s L's): slots N=%ld L=%ld E=%ld keys N=%u L=%u E=%u key_res=%u; L.sb=%s N.sb=%s releases=%u first at %.1f s; frames the escort headed=%u of 3000\n",
            inverted, inverted ? "<" : ">", long(N - g->m), long(L - g->m), long(E - g->m), N->key, L->key, E->key, E->key_res,
            gc::sb_name(L->sb), gc::sb_name(N->sb), rel, first_rel < 0 ? -1.0 : first_rel / 10.0, esc_head);
    }
}
