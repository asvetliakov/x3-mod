#include "game_phases_core.h"
#include <cstdio>
#include <cstdlib>
using namespace x3m::game_phases::detail;
static unsigned checks=0;
static void check(bool b){++checks;if(!b){std::fprintf(stderr,"check %u failed\n",checks);std::abort();}}
static Stamp at(std::uint64_t q,bool cpu=true){return {q,q/2,q/4,q+1,cpu,q};}
static Core core;
static std::uint64_t time_now=100;
static Present last;
static void reset(){core={};core.frequency=1000000;time_now=100;last={};}
// Ordinary full loop, with an optional400ms synchronous event inside cockpit.
static void loop(unsigned frame,bool slow=false,bool finish=true,std::uint64_t reset_id=0){
    for(unsigned i=0;i<13;++i){
        time_now+=100;core.boundary(i,at(time_now));
        if(i==8&&slow){core.begin(0,at(time_now+10),{0x10,0x20,3});time_now+=400000;core.end(0,at(time_now),1);}
    }
    time_now+=100;core.begin(3,at(time_now),{},0x1234);
    time_now+=100;last={7,reset_id,frame,time_now,0x1234,0,false};core.bridge(last,at(time_now+2));
    if(finish){time_now+=100;core.end(3,at(time_now),0);time_now+=100;core.boundary(13,at(time_now));}
}
int main(){
    reset();loop(0);const auto previous=last;
    check(core.frame_count==0);check(core.anchor_valid);check(core.slow_frames.count==0);
    loop(1,true,false);const auto endpoint=last;
    check(core.frame_count==1);check(core.slow_frames.count==0);check(core.depth==1); // notfinalizedinsideproxy
    check(core.stack[0].detail);check(core.stack[0].staged.current.frame==1);
    time_now+=3000;core.end(3,at(time_now),0);
    check(core.slow_frames.count==1);const auto& f=core.slow_frames.first[0];
    check(f.previous.frame==0&&f.current.frame==1);check(f.previous.qpc==previous.qpc&&f.current.qpc==endpoint.qpc);
    check(f.dispatch_end==time_now&&f.dispatch_end>f.current.qpc);
    check(f.covered==f.current.qpc-f.previous.qpc);check(!f.overflow);
    unsigned cockpit=0;std::uint64_t sum=0;
    for(unsigned i=0;i<f.used;++i){const auto& s=f.segments[i];
        check(s.begin.qpc>=f.previous.qpc&&s.end.qpc<=f.current.qpc);sum+=s.end.qpc-s.begin.qpc;
        check(s.begin.cpu&&s.end.cpu);
        check(s.begin.query_begin>=s.begin.qpc&&s.end.query_begin>=s.end.qpc);
        if(s.phase==8){++cockpit;check(s.end.qpc-s.begin.qpc>=400000);check(s.request_end.target==0x20);}
        if(i)check(s.begin.qpc==f.segments[i-1].end.qpc);
    }
    check(cockpit==1&&sum==f.covered);check(core.slow_calls.count==1);check(core.slow_calls.first[0].kind==0);
    check(core.slow_calls.first[0].end.qpc-core.slow_calls.first[0].begin.qpc==399990);
    check(core.frame_max_begin==previous.qpc&&core.frame_max_end==endpoint.qpc&&core.frame_max_id==1);
    time_now+=100;core.boundary(13,at(time_now));loop(2);
    check(core.used==1);check(core.tape[0].begin.qpc==last.qpc); // preceding proxy-tail nowbelongsnextframe

    reset();loop(0);loop(1,true,false);core.invalidate(); // failedReset invalidatespendingstagedframe
    check(!core.depth&&!core.anchor_valid&&!core.phase_live);check(core.slow_frames.count==0);
    core.end(3,at(time_now+100),0);check(core.unmatched==1);check(core.slow_frames.count==0);
    loop(2,false,true,1);check(core.anchor.reset==1&&core.frame_count==1); // oldcompletedcountretained, nointervalcrossReset
    check(core.slow_frames.count==0);

    reset();loop(0);loop(1,true,false);core.invalidate(); // finalRelease/recreatedpointercannotcompleteoldtoken
    core.end(3,at(time_now+1),0);check(core.slow_frames.count==0);
    loop(0);check(core.anchor.device==7&&core.anchor.frame==0);

    reset();core.boundary(8,at(100));check(core.unmatched==1&&!core.phase_live);
    core.boundary(0,at(101));core.boundary(2,at(102));check(core.order_errors==1&&!core.phase_live);
    core.boundary(0,at(103));core.boundary(1,at(102));check(core.clock_errors==1&&!core.phase_live);
    core.boundary(0,at(104));core.boundary(1,Stamp{});check(core.clock_errors==2&&!core.phase_live);
    core.boundary(0,at(105));core.boundary(0,at(106));check(core.order_errors==2&&core.phase_live);
    core.boundary(14,at(107));check(!core.phase_live);

    reset();core.boundary(0,at(100));
    for(unsigned i=0;i<stack_capacity;++i)core.begin(2,at(110+i));
    core.begin(2,at(130));check(core.overflow==1&&!core.depth&&!core.phase_live);
    core.boundary(0,at(140));core.begin(1,at(150));core.end(2,at(160));check(core.unmatched==1&&!core.depth);

    reset();loop(0); // metadata/raw/HRESULTmustmatchtheopendispatch
    core.boundary(0,at(time_now+100));core.begin(3,at(time_now+101),{},0x3333);
    core.bridge({7,0,1,time_now+102,0x4444,0,false});check(core.unmatched==1&&!core.stack[0].bridged);
    core.end(3,at(time_now+103),0);check(!core.anchor_valid);
    core.boundary(0,at(time_now+200));core.begin(3,at(time_now+201),{},0x3333);
    core.bridge({9,2,8,time_now+202,0x3333,0x80000000,true});core.end(3,at(time_now+203),0);
    check(!core.anchor_valid&&!core.slow_frames.count);

    reset();loop(0);loop(1,true,false);
    const auto old=core.stack[0].staged.current.qpc;
    core.clear_window();check(core.depth==1&&core.stack[0].detail); // reporting cannotflushunfinishedPresent
    core.end(3,at(time_now+100),0);check(core.slow_frames.count==1&&core.slow_frames.first[0].current.qpc==old);

    Metric metric;metric.add(at(100),at(200));check(metric.cpu_valid==1&&metric.user==50&&metric.kernel==25);
    metric.add(at(200,false),at(300));check(metric.count==2&&metric.cpu_valid==1);
    auto backwards=at(400);backwards.user=1;metric.add(at(300),backwards);check(metric.count==3&&metric.cpu_valid==1);
    Window<unsigned> window;for(unsigned i=0;i<25;++i)window.add(i);
    check(window.count==25&&window.first_used==4&&window.recent_used==4);
    for(unsigned i=0;i<4;++i){check(window.first[i]==i);check(window.recent[(window.next+i)%4]==21+i);}

    reset();loop(0);
    // Many complete loopswithoutPresentoverflowthetape; neverpretendcompletecoverage.
    for(unsigned round=0;round<9;++round){for(unsigned i=0;i<14;++i){time_now+=1000;core.boundary(i,at(time_now));}}
    core.begin(3,at(++time_now),{},0x1234);last={7,0,1,++time_now,0x1234,0,false};core.bridge(last);core.end(3,at(++time_now),0);
    check(core.slow_frames.count==1&&core.slow_frames.first[0].overflow);check(core.overflow>0);
    check(core.slow_frames.first[0].covered<core.slow_frames.first[0].current.qpc-core.slow_frames.first[0].previous.qpc);
    reset();loop(0);core.pump={9,10,true};
    time_now+=100;core.boundary(0,at(time_now));
    check(!core.pump.valid);check(core.tape[core.used-1].phase==13&&core.tape[core.used-1].pump.active==9);
    time_now+=100;core.boundary(1,at(time_now));
    check(core.tape[core.used-1].phase==0&&!core.tape[core.used-1].pump.valid);
    time_now+=100;core.boundary(2,at(time_now));
    check(core.tape[core.used-1].phase==1&&!core.tape[core.used-1].pump.valid);
    core.pump={1,2,true};time_now+=100;core.boundary(3,at(time_now));
    check(core.tape[core.used-1].phase==2&&core.tape[core.used-1].pump.valid&&core.tape[core.used-1].pump.flags==2);
    core.invalidate();check(!core.pump.valid&&core.pump.active==0&&core.pump.flags==0);
    std::printf("game_phases_host checks=%u failures=0 core_bytes=%zu\n",checks,sizeof(Core));
}
