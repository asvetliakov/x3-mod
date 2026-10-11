#include <cstdio>
#include <cstdint>
#include "chase_transition_restore_core.h"
using namespace x3m::chase_transition::detail;
struct P{bool active_control(uint32_t&h){h=7;return true;} bool ship_live(uint32_t,uint32_t&i){i=0xfffefa78u;return true;} bool controller(uint32_t&i){i=0xffff6eafu;return true;}};
int main(){RestoreState s;P p;RestoreFrame f;f.cockpit=0x1000;f.mode=258;f.ref=f.view=0x47e69ad0u;f.sector=1;f.generation=1;f.constructor_caller=restore_alloc_caller;f.readable=true;
s.on_frame(f,p,1);s.on_destroy(0x1000,1,restore_free_caller);s.on_complete(restore_alloc_caller);
s.on_mode_store(0x1000,1,11,restore_alloc_caller,0xffff6eaeu,0x25e);
RestoreFrame g=f;g.generation=2;g.mode=1;g.sector=2;s.on_frame(g,p,1);s.on_frame(g,p,1);
auto c=s.on_inject_entry(11,g,g.ref,0,p);
printf("phase_before=%u wanted=%d monitor=%x cancel_no_monitor=%llu\n",s.phase,c.wanted,s.monitor,(unsigned long long)s.cancels[cancel_no_monitor]);}
