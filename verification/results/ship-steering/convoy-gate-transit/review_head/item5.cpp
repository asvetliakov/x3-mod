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
  g->lane_c[0]=0; g->lane_c[1]=-929; g->lane_c_ok=1; g->step_ms=17;
  auto mk=[&](gc::Member& m, gc::Role role, unsigned short order){ m.ship=m.id=0x9100; m.role=role; m.ln=1; m.q_ok=1; m.q[0]=0; m.q[1]=-700;
    m.r=1022; m.w=540; m.L=1022; m.params.v_max=113; m.params.W=0.077f; m.params.alpha=0.03f; m.params.a_lin=5; m.params.r_gate=1105; m.large=1;
    m.sb=gc::Sb::funnel; m.v_sb=100; m.merged=1; m.live_ms=m.decided_ms=m.commit_ms=1000; m.st.ms=1000; m.st.order=order; m.st.target=order?0x4000u:0u;
    m.st.f_ok=1; m.st.v=100; for(int r=0;r<3;++r) for(int c=0;c<3;++c) m.st.rows[r][c]= r==c; g->n=1; };
  gc::Member& m=g->m[0]; mk(m,gc::Role::escort,core::kOrderNone); m.lane_esc=1; m.res_until=30000; m.res_o8_ms=500; m.steered_ms=900;
  const float a=20*M_PI/180; m.st.x[0]=0; m.st.x[1]=-1529; m.st.x[2]=-9000; m.st.rows[2][0]=0; m.st.rows[2][1]=sinf(a); m.st.rows[2][2]=cosf(a);
  m.st.rows[1][1]=cosf(a); m.st.rows[1][2]=-sinf(a); for(int i=0;i<3;++i) m.st.u[i]=100*m.st.rows[2][i];
  static gc::Sample sm[gc::kSamples]; float vmax=0; unsigned ns=gc::rollout(m,*g,-1,sm,&vmax,1000); double dev=0;
  for (unsigned k=0;k<ns && k*gc::kSampleS<10;++k){ double t=k*gc::kSampleS, d2=0; for(int i=0;i<3;++i){double st=m.st.x[i]+m.st.rows[2][i]*100*t; d2+=(sm[k].c[i]-st)*(sm[k].c[i]-st);} dev=fmax(dev,sqrt(d2)); }
  printf("5a: straight-coast deviation over 10 s %.0f m (n %u)\n", dev, ns);
  gc::Member& c=g->m[0]; c=gc::Member{}; mk(c,gc::Role::committed,core::kOrderFlyBefore); c.steered_ms=1000; float lat[2]; gc::lane_lat_at(*g,c,-4200,lat);
  c.st.x[0]=lat[0]; c.st.x[1]=lat[1]; c.st.x[2]=-4200; c.st.u[2]=100; unsigned nb=gc::rollout(c,*g,-1,sm,&vmax,1000); double moved=-1;
  for(unsigned k=0;k<nb;++k) if (sm[k].c[2] <= -gc::kLaneEndM) moved=(sm[k].c[1]-lat[1])/(c.q[1]-lat[1]);
  printf("5b: blend moved %.0f %% (n %u)\n", 100*moved, nb);
}
