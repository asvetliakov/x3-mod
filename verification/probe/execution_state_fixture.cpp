#include "../../src/ownership/execution_state.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
using namespace x3m::ownership;
namespace {
unsigned checks = 0;
void check(bool value) { ++checks; if (!value) { std::printf("FAIL check=%u\n", checks); std::exit(1); } }
constexpr std::int32_t fail = static_cast<std::int32_t>(0x80004005u);
void reset(ObservedExecutionState& s) { s.before_reset(); check(!s.view().known); s.after_reset(0); }
void healthy(const ObservedExecutionState& s, bool scene=false, bool recording=false, unsigned active=0) {
    auto v=s.view(); check(v.requested&&v.known); check(v.scene_open==scene);
    check(v.stateblock_recording==recording); check(v.active_queries==active);
    check(v.queries_idle==(active==0)); check(v.reason==ExecutionReason::None);
}
void basics() {
    static_assert(!std::is_copy_constructible<ObservedExecutionState>::value, "device identity must not copy");
    static_assert(!std::is_copy_constructible<ExecutionQuery>::value, "query identity must not copy");
    ObservedExecutionState off; check(!off.view().known&&!off.view().queries_idle);
    off.initialize(false); off.begin_scene(0); off.before_reset(); off.after_reset(0);
    check(!off.view().requested&&!off.view().known&&off.view().generation==0);
    ObservedExecutionState s;s.initialize(true);healthy(s);check(s.view().generation==1);
    s.begin_scene(0);healthy(s,true);s.begin_stateblock(0);healthy(s,true,true);
    s.end_stateblock(0);healthy(s,true);s.end_scene(0);healthy(s);
    reset(s);healthy(s);check(s.view().generation==2);
    // Reinitializing a live device cannot erase observation gaps.
    s.initialize(true);check(!s.view().known);reset(s);check(!s.view().known);
}
void transitions() {
    for(unsigned operation=0;operation<4;++operation) {
        for(auto hr : {fail, std::int32_t{1}, static_cast<std::int32_t>(0x88760868u),static_cast<std::int32_t>(0x88760869u)}) {
            ObservedExecutionState s;s.initialize(true);
            if(operation==1)s.begin_scene(0);if(operation==3)s.begin_stateblock(0);
            if(operation==0)s.begin_scene(hr);if(operation==1)s.end_scene(hr);
            if(operation==2)s.begin_stateblock(hr);if(operation==3)s.end_stateblock(hr);
            check(!s.view().known&&!s.view().queries_idle);
            reset(s);healthy(s);
        }
    }
    for(unsigned operation=0;operation<4;++operation) {
        ObservedExecutionState s;s.initialize(true);
        if(operation==0){s.begin_scene(0);s.begin_scene(0);}
        if(operation==1)s.end_scene(0);
        if(operation==2){s.begin_stateblock(0);s.begin_stateblock(0);}
        if(operation==3)s.end_stateblock(0);
        check(!s.view().known);reset(s);healthy(s);
    }
    ObservedExecutionState s;s.initialize(true);s.observe_result(fail);healthy(s);
    s.observe_result(static_cast<std::int32_t>(0x88760868u));check(!s.view().known);
    s.before_reset();s.after_reset(fail);check(!s.view().known&&s.view().generation==1);
    reset(s);healthy(s);
    s.unknown_native_execution();reset(s);check(!s.view().known&&s.view().reason==ExecutionReason::NativeBypass);
}
void queries() {
    ObservedExecutionState s;s.initialize(true);ExecutionQuery a,b;
    s.query_created(a,9);s.query_created(b,9);healthy(s);
    s.query_issue(a,2,0);healthy(s,false,false,1);
    s.query_issue(b,2,0);healthy(s,false,false,2);
    s.query_issue(a,1,0);healthy(s,false,false,1);
    s.query_issue(b,1,0);healthy(s);
    // Completion/readback is unnecessary: END closes the measured draw interval.
    reset(s);s.query_issue(a,2,0);healthy(s,false,false,1);
    s.query_issue(a,1,0);s.query_destroyed(a);s.query_destroyed(b);healthy(s);
    for(auto type : {0u,4u,5u,6u,8u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,0xffffffffu}) {
        ObservedExecutionState d;d.initialize(true);ExecutionQuery q;d.query_created(q,type);
        check(!d.view().known&&d.view().reason==ExecutionReason::UnsupportedQuery);
        d.query_destroyed(q);reset(d);check(!d.view().known&&!d.view().queries_idle);
    }
    for(unsigned bad=0;bad<10;++bad) {
        ObservedExecutionState d;d.initialize(true);ExecutionQuery q;
        if(bad!=0)d.query_created(q,9);
        switch(bad) {
        case 0:d.query_issue(q,2,0);break;
        case 1:d.query_issue(q,1,0);break;
        case 2:d.query_issue(q,2,0);d.query_issue(q,2,0);break;
        case 3:d.query_issue(q,0,0);break;
        case 4:d.query_issue(q,3,0);break;
        case 5:d.query_issue(q,2,fail);break;
        case 6:d.query_issue(q,2,0);d.query_issue(q,1,fail);break;
        case 7:d.query_issue(q,2,0);d.query_destroyed(q);break;
        case 8:d.query_created(q,9);break;
        case 9:d.query_issue(q,2,0);d.before_reset();d.after_reset(0);break;
        }
        check(!d.view().known&&!d.view().queries_idle);reset(d);check(!d.view().known);
    }
    for(bool before_begin : {false,true}) {
        ObservedExecutionState d;d.initialize(true);ExecutionQuery q;d.query_created(q,9);
        if(!before_begin)d.query_issue(q,2,0);
        d.observe_result(static_cast<std::int32_t>(0x88760868u));
        if(before_begin)d.query_issue(q,2,0);
        d.query_issue(q,1,0);reset(d);check(!d.view().known&&!d.view().queries_idle);
    }
    ObservedExecutionState x,y;x.initialize(true);y.initialize(true);ExecutionQuery q;x.query_created(q,9);
    y.query_issue(q,2,0);check(!y.view().known);healthy(x);
    ObservedExecutionState r;r.initialize(true);r.after_reset(0);check(!r.view().known);
    reset(r);check(!r.view().known);
    ObservedExecutionState n;n.initialize(true);n.before_reset();n.before_reset();n.after_reset(0);
    check(!n.view().known);
}
void randomized_intervals() {
    ObservedExecutionState s;s.initialize(true);std::array<ExecutionQuery,16> query;
    bool active[16]{};unsigned count=0;std::uint32_t seed=0x715916b1;
    for(auto& q:query)s.query_created(q,9);
    for(unsigned n=0;n<10000;++n) {
        seed=1664525u*seed+1013904223u;unsigned i=(seed>>20)&15;
        s.query_issue(query[i],active[i]?1:2,0);
        count=active[i]?count-1:count+1;active[i]=!active[i];
        healthy(s,false,false,count);
    }
    for(unsigned i=0;i<16;++i) {
        if(active[i])s.query_issue(query[i],1,0);
        s.query_destroyed(query[i]);
    }
    healthy(s);check(s.view().refusals==0);check(s.view().observed_calls>=10032);
}
}
int main(){basics();transitions();queries();randomized_intervals();std::printf("RESULT PASS checks=%u\n",checks);}
