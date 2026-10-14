#include <initializer_list>
#include <cstdio>
#include <cmath>
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
namespace core = x3m::ship_steering::core;
int main() {
  static gc::Board b{}; b.lane = 1;
  const float G[3]={0,0,0},R[3]={1,0,0},U[3]={0,1,0},F[3]={0,0,1};
  gc::Gate* g = gc::acquire_gate(b, 0x4000, 1000, G, R, U, F, 1105.0f, 0);
  auto mk = [&](unsigned i, unsigned ship, float z, float x, float vmax) {
    gc::Member& m = g->m[i]; m.ship=ship; m.id=ship; m.role=gc::Role::committed; m.seq=i+1; m.r=1000; m.w=530; m.L=1000; m.params.v_max=vmax;
    m.params.W=0.1f; m.params.alpha=0.05f; m.params.a_lin=5; m.large=1; m.live_ms=1000; m.proven=1; m.decided_ms=1000; m.commit_ms=1000; m.steered_ms=1000;
    m.line_ok=1; m.st.ms=1000; m.st.order=core::kOrderFlyBefore; m.st.target=0x4000; m.st.x[0]=x; m.st.x[1]=-929; m.st.x[2]=z;
    m.st.rows[2][2]=1; m.st.f_ok=1; m.st.v=vmax<100?vmax:100; m.st.u[2]=m.st.v; if (i+1>g->n) g->n=i+1; };
  mk(0,0x9000,-10000,0,60); mk(1,0x9100,-12500,0,110); mk(2,0x9200,-11300,-3000,110);
  g->m[2].st.rows[2][0]=0.3f; g->m[2].st.rows[2][2]=0.954f;
  gc::LaneStats ls; gc::lane_frame(*g,1000,&ls); gc::lane_frame(*g,1100,&ls);
  printf("A rank %u law %s v %.1f | B rank %u | C rank %u\n", g->m[0].lane_rank, gc::law_name(g->m[0].law), g->m[0].v_sb, g->m[1].lane_rank, g->m[2].lane_rank);
}
