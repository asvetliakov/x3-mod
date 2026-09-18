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
static void loop(unsigned frame,bool slow=false,bool finish=true,std::uint64_t reset_id=0,std::uint64_t stall=400000){
    for(unsigned i=0;i<13;++i){
        time_now+=100;core.boundary(i,at(time_now));
        if(i==8&&slow){core.begin(0,at(time_now+10),{0x10,0x20,3});time_now+=stall;core.end(0,at(time_now),1);}
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
    // Input subdivisions retain an exact additive partition and real CPU
    // samples, without changing the parent phase IDs or Present interval.
    reset();loop(0);time_now+=100;core.boundary(0,at(time_now));
    for(unsigned i=1;i<=6;++i)core.boundary(i,at(time_now+=100));
    core.input_boundary(1,at(time_now+=300));core.input_boundary(2,at(time_now+=500));
    core.boundary(7,at(time_now+=700));
    check(core.input_parts[0].total==400); // includes first ordinary100us loop
    check(core.input_parts[1].total==500&&core.input_parts[2].total==700);
    check(core.phases[6].total==1600);
    check(core.tape[core.used-3].input_part==0&&core.tape[core.used-2].input_part==1&&core.tape[core.used-1].input_part==2);
    core.input_boundary(1,at(++time_now));check(core.order_errors==1&&!core.phase_live);
    // The last completed Input phase (1500 us) was latched for the loop-phase
    // join before that order error; the invalidation it caused clears the latch
    // so no stale value survives until phase 6 completes again.
    check(!core.input_valid&&core.input_last==0&&core.input_ticks==0);
    reset();loop(0);time_now+=100;core.boundary(0,at(time_now));
    for(unsigned i=1;i<=6;++i)core.boundary(i,at(time_now+=100));
    check(!core.input_valid||core.input_last==100); // loop(0)'s 100 us Input phase, not the one in progress
    core.boundary(7,at(time_now+=250));check(core.input_valid&&core.input_last==250&&core.input_ticks==0);
    core.invalidate();check(!core.input_valid&&core.input_last==0);

    reset();loop(0);core.boundary(0,at(time_now+=100));
    for(unsigned i=1;i<=6;++i)core.boundary(i,at(time_now+=100));
    const auto base=time_now;Witness publisher;publisher.end_esp=0x1000;publisher.caller=0x42dd6e;
    publisher.previous_target=0x11;publisher.view=0x88;
    core.target_begin(4,at(base+10,false),{0x55,0x66,3},publisher);
    check(core.request_for(4).target==0x66);
    Witness playback;playback.end_esp=0xf00;playback.args[0]=7;
    core.target_begin(5,at(base+20,false),core.request,playback);
    Witness create;create.end_esp=0xe04;
    core.target_begin(6,at(base+30,false),core.request,create);
    core.target_end(6,at(base+80,false),0xe04,0x99);
    Witness seek;seek.end_esp=0xe00;
    core.target_begin(7,at(base+100,false),core.request,seek);
    core.target_end(7,at(base+200,false),0xe00,0);
    core.target_end(5,at(base+300,false),0xf00,1);
    core.target_end(4,at(base+400,false),0x1000,1);
    check(core.depth==0&&core.first_mask==0xf0&&core.pending_first==0xf0);
    check(core.request_for(4).target==0);
    check(core.first_calls[0].children==280&&core.first_calls[1].children==150);
    check(core.first_calls[0].witness.caller==0x42dd6e&&core.first_calls[0].request.target==0x66);
    check(core.first_calls[1].witness.args[0]==7&&core.first_calls[1].phase==6);
    check(core.first_calls[1].present.frame==0&&core.first_calls[1].present.device==7);
    check(core.slow_calls.count==0);check(core.calls[7].count==1&&core.calls[7].cpu_valid==0);
    core.clear_window();check(core.pending_first==0&&core.first_mask==0xf0);
    core.target_begin(4,at(base+500,false),{0x55,0,3},publisher);
    core.target_end(4,at(base+510,false),0x2000,0); // unrelated shared join
    check(core.depth==1&&core.ignored_joins==1);
    core.target_end(4,at(base+520,false),0x1000,0); // null request valid short return
    check(core.depth==0&&core.pending_first==0&&core.slow_calls.count==0);
    core.target_begin(4,at(base+600,false),{0x55,0x77,3},publisher);
    core.target_end(4,at(base+20000,false),0x1000,1);
    check(core.slow_calls.count==1&&core.slow_calls.first[0].request.target==0x77);
    check(!core.slow_calls.first[0].first);
    core.target_begin(4,at(base+21000,false),{0x55,0x99,3},publisher);
    core.invalidate();core.target_end(4,at(base+22000,false),0x1000,1);
    check(!core.depth&&!core.anchor_valid&&core.ignored_joins==2);
    check(core.slow_calls.count==1&&core.first_mask==0xf0); // no first-sample flood afterReset
    core.boundary(0,at(base+23000));
    core.target_begin(4,at(base+23001),{},publisher);check(!core.depth); // outside allowedphase
    for(unsigned i=1;i<=4;++i)core.boundary(i,at(base+23002+i));
    core.target_begin(5,at(base+24000),{},playback);check(core.depth==1); // queued playback withoutpublisher
    core.target_end(5,at(base+54000),0xf00,0);
    check(core.slow_calls.count==2&&core.slow_calls.first[1].phase==4);
    core.target_begin(5,at(base+55000),{},playback);
    core.boundary(0,at(base+56000));check(!core.depth&&core.order_errors==1); // escaped native call revoked
    // Present-cadence loading markers: 1 kHz clock, 3 s stall threshold.
    LoadingPhases lp;lp.stall_ticks=3000;LoadingPhases::Marker m[2];
    check(lp.present(1,0,0,1000,m)==0);check(lp.present(1,0,1,2800,m)==0); // splash gap under threshold
    check(lp.present(1,0,2,4500,m)==0);check(lp.present(1,0,3,7499,m)==0); // 2999 ticks: not a stall
    check(lp.present(1,0,4,13000,m)==1&&m[0].name==LoadingPhases::MenuShown&&m[0].frame==4&&m[0].qpc==13000&&m[0].stall==5501);
    for(unsigned f=5;f<60;++f)check(lp.present(1,0,f,13000+(f-4)*20,m)==0); // menu cadence
    check(lp.present(1,0,60,13000+56*20,m)==0);
    check(lp.present(1,1,61,13000+56*20+9000,m)==0);check(lp.last_reset==1); // in-place Reset pause is not a stall
    check(lp.present(2,0,0,13000+56*20+9000+8000,m)==0);check(lp.last_device==2); // device change is not a stall
    check(lp.present(2,0,1,31140,m)==0);check(lp.present(2,0,2,31160,m)==0);
    check(lp.present(2,0,3,52140,m)==2);
    check(m[0].name==LoadingPhases::SaveLoadBegin&&m[0].frame==2&&m[0].qpc==31160&&m[0].stall==0);
    check(m[1].name==LoadingPhases::SaveLoadComplete&&m[1].frame==3&&m[1].qpc==52140&&m[1].stall==20980);
    check(lp.emitted==7);check(lp.present(2,0,4,80000,m)==0); // later stalls (sector change) never re-emit
    check(lp.present(2,0,5,70000,m)==0); // clock going backwards re-anchors without a marker
    LoadingPhases unset;check(unset.present(1,0,0,0,m)==0&&unset.present(1,0,1,1u<<30,m)==0); // no threshold, no markers
    LoadingPhases early;early.stall_ticks=3000; // a stall between the first two Presents is labelled menu_shown
    check(early.present(1,0,0,1000,m)==0);check(early.present(1,0,1,9000,m)==1&&m[0].name==LoadingPhases::MenuShown&&m[0].frame==1);
    LoadingPhases pair;pair.stall_ticks=3000; // two devices presenting alternately never anchor a same-device gap
    for(unsigned i=0;i<6;++i)check(pair.present(1+(i&1),0,i,1000+i*5000,m)==0);
    check(pair.emitted==0);
    // Segment tape threshold. Default (frame_threshold==0): the built-in 50 ms,
    // so a 25 ms frame stages nothing. The runtime sets frame_threshold from
    // X3M_GAME_PHASE_THRESHOLD_MS (launcher --game-phase-threshold-ms, 20 ms
    // default), and the same 25 ms frame is staged with its segment rows.
    reset();loop(0);loop(1,true,false,0,25000);
    check(!core.stack[0].detail);
    time_now+=100;core.end(3,at(time_now),0);check(core.slow_frames.count==0);

    reset();core.frame_threshold=core.frequency*20/1000;loop(0);loop(1,true,false,0,25000);
    check(core.stack[0].detail&&core.stack[0].staged.current.frame==1);
    time_now+=100;core.end(3,at(time_now),0);
    check(core.slow_frames.count==1);
    {
        const auto& t=core.slow_frames.first[0];
        check(t.used>0&&t.current.qpc-t.previous.qpc>=25000);
        unsigned stalled=0;
        for(unsigned i=0;i<t.used;++i)if(t.segments[i].phase==8&&t.segments[i].end.qpc-t.segments[i].begin.qpc>=25000)++stalled;
        check(stalled==1);
    }
    // A frame under the configured threshold still stages nothing.
    reset();core.frame_threshold=core.frequency*20/1000;loop(0);loop(1,true,false,0,10000);
    check(!core.stack[0].detail);
    time_now+=100;core.end(3,at(time_now),0);check(core.slow_frames.count==0);

    std::printf("game_phases_host checks=%u failures=0 core_bytes=%zu\n",checks,sizeof(Core));
}
