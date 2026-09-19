#include "../../src/proxy/light_phases_core.h"
#include <cstdio>
using namespace x3m::light_phases::detail;
unsigned checks=0,failures=0;
void check(bool b){++checks;if(!b)++failures;}
int main(){
    Gate g;check(!g.owned(7)&&g.early==1);check(g.admit(7));check(!g.owned(8)&&g.foreign==1);check(g.owned(7));
    Accumulator a;Sample s;
    for(unsigned i=0;i<3;++i){a.stamp(0,100,i==0?cockpit_return:i==1?traversal_return:0,10);a.stamp(1,100,0,30);}
    a.take(5,1000000,s);check(s.frame==5&&s.stamps==6);
    for(unsigned i=0;i<3;++i)check(s.entries[i]==1&&s.calls[i]==1&&s.us[i]==20&&s.ticks[i]==20);
    a.stamp(0,100,cockpit_return,50);a.stamp(0,80,cockpit_return,55);a.stamp(1,80,0,60);a.stamp(1,100,0,70);
    check(a.errors.nested==1&&a.pending.calls[0]==0&&a.depth==0);a.discard();
    for(unsigned i=0;i<10;++i)a.stamp(0,100-i*4,traversal_return,100+i);
    check(a.errors.overflow==2&&a.depth==max_depth);a.discard();check(a.errors.unmatched==8);
    a.stamp(0,100,cockpit_return,50);a.stamp(1,104,0,60);check(a.errors.mismatch==1&&a.depth==0);a.discard();
    a.stamp(0,100,cockpit_return,50);a.take(6,1000000,s);check(a.errors.unmatched==10&&a.depth==0);
    a.stamp(1,100,0,1000000);check(a.pending.calls[0]==0);a.discard();
    a.stamp(0,100,cockpit_return,0);a.stamp(1,100,0,10);check(a.errors.clock_failures==1&&a.pending.calls[0]==0);a.discard();
    a.stamp(0,100,cockpit_return,20);a.stamp(1,100,0,0);check(a.errors.clock_failures==2&&a.pending.calls[0]==0);a.discard();
    a.stamp(0,100,cockpit_return,20);a.stamp(1,100,0,10);check(a.errors.clock_reversal==1&&a.pending.calls[0]==0);a.discard();
    a.stamp(0,100,cockpit_return,20);a.interrupted();a.stamp(1,100,0,30);check(a.errors.reentry==1&&a.pending.calls[0]==0);a.discard();
    a.stamp(0,100,cockpit_return,20);a.stamp(1,100,0,40);a.take(7,1000000,s);check(s.calls[0]==1&&s.us[0]==20);
    Window w;Summary out;check(!w.close(out));
    for(unsigned i=0;i<window_frames;++i){s={};s.valid=true;s.frame=i;s.calls[0]=i;s.us[0]=i;s.ticks[0]=i;s.entries[0]=i;s.stamps=i;s.self_us=i;w.add(s);}
    check(w.full());check(w.close(out));check(out.frames==300&&out.frame==299&&out.calls_p50[0]==150&&out.us_p95[0]==285&&out.calls[0]==44850&&out.ticks[0]==44850&&out.self_p95==285);
    a.stamp(0,100,cockpit_return,10);a.stamp(1,100,0,30);
    a.stamp(0,100,cockpit_return,40);a.stamp(0,80,cockpit_return,45);
    check(!a.take(301,1000000,s)&&!s.valid&&s.calls[0]==0&&s.ticks[0]==0);
    w.add(s);check(w.close(out)&&out.valid_frames==0&&out.invalid_frames==1&&out.us_p50[0]==0);
    a.stamp(0,100,cockpit_return,10);check(!a.take(302,1000000,s)&&!s.valid);
    a.refuse_mode();check(!a.take(303,1000000,s)&&a.errors.mode_refused==1);
    check(a.take(304,1000000,s)&&s.valid);w.add(s);
    s.valid=false;s.calls[0]=999;s.us[0]=999;w.add(s);
    check(w.close(out)&&out.frames==2&&out.valid_frames==1&&out.invalid_frames==1&&out.calls[0]==0&&out.us_p95[0]==0);
    std::printf("light_phases_host checks=%u failures=%u accumulator_bytes=%zu window_bytes=%zu\n",checks,failures,sizeof a,sizeof w);
    return failures?1:0;
}
