#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <cmath>
// Review probe (step-2 review, finding 2): an M5 at 100 m/s head-on to a ship d metres ahead must never be
// suppressed at site E inside its stopping (250 m) or turn-in distance (72 m). Build with -I src/proxy; the host test
// verification/analysis/test_ship_steering.py runs it and asserts suppressed=0 on every line.
#include "ship_avoidance_core.h"
namespace av = x3m::ship_avoidance::core;
static av::Snapshot snap;
int main(){
  for (float d : {45.f, 55.f, 70.f, 100.f, 200.f}) {
    snap = av::Snapshot{}; snap.sector=1; snap.count=2; snap.max_r=9.2f;
    snap.items[0] = av::Obstacle{}; snap.items[0].ptr=0x1000; snap.items[0].r=9.2f; snap.items[0].kind=0;
    snap.items[1] = av::Obstacle{}; snap.items[1].ptr=0x2000; snap.items[1].r=9.2f; snap.items[1].kind=0; snap.items[1].x[2]=d;
    av::Self s{}; s.f[2]=1; s.v=100; s.r=9.2f; s.a=20; s.omega=2.58f; s.alpha=1.29f; s.up[1]=1; s.ptr=0x1000;
    av::ShipAvoid st{}; st.obj=0x1000;
    float P[3]={0,0,20000};
    av::Result r = av::resolve(snap, nullptr, 0, s, P, st, 1000);
    float C=av::clearance(9.2f,9.2f); float R=100/2.58f;
    std::printf("d=%5.0f adjusted=%d unresolved=%d n_resolved=%u suppressed=%d C=%.1f d_turn=%.1f D_stop=%.0f P'=%.1f,%.1f,%.1f\n", d, r.adjusted, r.unresolved, st.n_resolved,
      av::suppressed(st, 0x1000, 0x2000, 1050), C, std::sqrt(C*(C+2*R)), 100.f*100.f/40.f, r.P[0], r.P[1], r.P[2]);
  }
}
