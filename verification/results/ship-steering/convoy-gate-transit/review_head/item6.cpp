#include <initializer_list>
#include <cstdio>
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
int main(){ gc::LaneCtx y; y.s=-10000; y.v_lane=100; y.a=5; y.pred=true; y.merged=false; y.gap=-85; y.v_self=10; y.v_pred=100;
  gc::Law l; bool m=false; float v=gc::lane_law(y,&l,&m); printf("raw -85, settling +810: merged %d law %s v %.1f\n", int(m), gc::law_name(l), v); }
