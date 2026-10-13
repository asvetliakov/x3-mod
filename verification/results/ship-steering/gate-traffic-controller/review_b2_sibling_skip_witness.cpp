// Review witness 2 (B.2 at 4b12b089): a committed sibling escort (its own order 4 while its leader holds; commit_fill clears its leader,
// key_res kept) is not re-keyed by its leader's head skip (the skip moves Role::escort only), so seq_tuple's key-class lookup
// (m.key & ~15) no longer finds the leader: the sibling ranks on its old key, ahead of the leader, and is released first.
// It is also accepted as the skip's "non-escort" feasible alternative. Build from the repository root:
//   c++ -std=c++17 -O2 -w verification/results/ship-steering/gate-traffic-controller/review_b2_sibling_skip_witness.cpp -o /tmp/w2 && /tmp/w2
#define main fixture_main
#include "../../../probe/ship_steering_host.cpp"
#undef main
static gc::Member* put_titan(gc::Board& bd, gc::Gate& g, unsigned k, int slot, gc::Role role, std::uint16_t order, const float off[3], float W, std::uint32_t now) {
    double A[3]; float Af[3]; core::GateGeometry geo; bool valid = false;
    capital_grid(kCrowdTitan, slot, kGateIdentity, 0.0, A, Af, &geo, &valid);
    const float x[3] = {geo.X[0] + off[0], geo.X[1] + off[1], geo.X[2] + off[2]}, F[3] = {0, 0, 1};
    gc::Member* m = ctl_put(bd, g, 0x9000 + 0x100 * k, role, order, x, F, 40.0f, 1092, 583, now);
    for (int i = 0; i < 3; ++i) { m->X[i] = geo.X[i]; m->h[i] = geo.h[i]; }
    m->line_ok = role == gc::Role::committed ? 1 : 0;
    m->params.W = W; m->params.alpha = W * 0.05f; m->params.a_lin = 10.0f; m->params.v_max = 110.0f;
    return m;
}
int main() {
    static gc::Board bd; bd = gc::Board{};
    std::uint32_t now = 400000;
    gc::Gate* g = ctl_gate(bd, now);
    const float offL[3] = {0.0f, -2500.0f, -4000.0f}, offE[3] = {-2500.0f, -1500.0f, -6000.0f}, offN[3] = {2000.0f, -2500.0f, -4000.0f};
    gc::Member* L = put_titan(bd, *g, 0, 0, gc::Role::committed, core::kOrderFlyBefore, offL, 0.0f, now); // never feasible
    gc::Member* E = put_titan(bd, *g, 1, 1, gc::Role::escort, core::kOrderEscort, offE, 0.157f, now);
    gc::Member* N = put_titan(bd, *g, 3, 3, gc::Role::committed, core::kOrderFlyBefore, offN, 0.157f, now);
    E->leader = L->ship; E->st.target = L->ship;
    N->enter_ms = now + 10000; // no burst
    bool converted = false; unsigned skips = 0; std::uint32_t first_rel = 0;
    for (int it = 0; it < 3000; ++it, now += 100) {
        for (unsigned k = 0; k < g->n; ++k) { g->m[k].st.ms = g->m[k].decided_ms = now; if (g->m[k].role == gc::Role::committed) g->m[k].commit_ms = now; }
        if (!converted && E->key_res) { // the escort gets its own order 4 while its leader holds (commit_fill clears its leader)
            E->role = gc::Role::committed; E->line_ok = 1; E->st.order = core::kOrderFlyBefore; E->st.target = g->gate; E->leader = 0; converted = true;
        }
        gc::SbStats st; st.seq_on = true; gc::sb_frame(*g, now, &st);
        skips += st.seq_skipped;
        for (unsigned q = 0; q < st.n_ev; ++q) if (st.ev[q].why == std::uint8_t(gc::Why::release) && !first_rel) first_rel = st.ev[q].ship;
        if (skips && it % 100 == 0 && first_rel == 0) {}
    }
    std::printf("skips %u; keys L %u E %u N %u; E before L: %d; first release 0x%x (L 0x9000 E 0x9100 N 0x9300); E.sb %s L.sb %s\n", skips, L->key, E->key, N->key,
                gc::seq_before(*g, *E, *L) ? 1 : 0, first_rel, gc::sb_name(E->sb), gc::sb_name(L->sb));
}
