#pragma once
#include <algorithm>
#include <cstdint>

namespace x3m::collide_query_phases::core {
constexpr unsigned window_frames=300;
enum Kind { Miss, Verify, Ineligible };
enum Reason : unsigned { Clock=1, Reentry=2, Foreign=4, Unwind=8, Reset=16, CpuMode=32 };
struct Sample {
    std::uint64_t query=0,descent=0,non_descent=0,queries=0,descents=0,invalid=0;
    std::uint64_t misses=0,verifies=0,ineligible=0,visits=0,triangles=0,contacts=0;
    std::uint64_t clock=0,reentry=0,foreign=0,unwind=0,reset=0,cpu_mode=0;
};
// Owner-thread state. One invalid interval excludes its entire frame's costs
// from sums/quantiles. Counts and discard reasons still describe all attempts.
struct Accumulator {
    Sample frame{};
    std::uint64_t start=0,descent_start=0,descent_ticks=0,descent_count=0;
    unsigned depth=0,reasons=0;
    Kind kind=Miss;
    bool pending=false;
    void begin(std::uint64_t t,Kind k=Miss){
        if(pending){invalidate(Reentry);return;}
        pending=true;reasons=t?0u:unsigned(Clock);start=t;descent_ticks=descent_count=0;depth=0;kind=k;
        if(k==Miss)++frame.misses;else if(k==Verify)++frame.verifies;else ++frame.ineligible;
    }
    void invalidate(Reason why=Reentry){if(pending)reasons|=why;}
    void enter(std::uint64_t t){
        if(!pending)return;
        if(depth++){invalidate(Reentry);return;}
        if(!t)invalidate(Clock);
        descent_start=t;
    }
    void leave(std::uint64_t t){
        if(!pending)return;
        if(!depth){invalidate(Unwind);return;}
        if(--depth)return;
        if(!t||t<descent_start){invalidate(Clock);return;}
        descent_ticks+=t-descent_start;++descent_count;
    }
    void record_invalid(){
        ++frame.invalid;frame.clock+=bool(reasons&Clock);frame.reentry+=bool(reasons&Reentry);
        frame.foreign+=bool(reasons&Foreign);frame.unwind+=bool(reasons&Unwind);frame.reset+=bool(reasons&Reset);frame.cpu_mode+=bool(reasons&CpuMode);
    }
    void end(std::uint64_t t,std::uint32_t visits=0,std::uint32_t triangles=0,std::uint32_t contacts=0){
        if(!pending)return;
        if(!t||t<start||descent_ticks>t-start)invalidate(Clock);
        if(depth)invalidate(Unwind);
        frame.visits+=visits;frame.triangles+=triangles;frame.contacts+=contacts;
        if(reasons)record_invalid();
        else {const auto query=t-start;frame.query+=query;frame.descent+=descent_ticks;
            frame.non_descent+=query-descent_ticks;++frame.queries;frame.descents+=descent_count;}
        pending=false;depth=0;
    }
    void abandon(Reason why=Unwind){if(pending){invalidate(why);record_invalid();}pending=false;depth=0;}
    void reset(){if(pending)abandon(Reset);else{++frame.invalid;++frame.reset;}}
    void reject_mode(){if(pending)invalidate(CpuMode);else{++frame.invalid;++frame.cpu_mode;}}
    Sample take(){abandon();const auto out=frame;frame={};return out;}
};
struct Window {
    Sample samples[window_frames]{};unsigned size=0;
    bool add(const Sample& s){if(size==window_frames)return false;samples[size++]=s;return true;}
    unsigned valid_frames()const{unsigned count=0;for(unsigned i=0;i<size;++i)count+=samples[i].invalid==0;return count;}
    std::uint64_t quantile(std::uint64_t Sample::*field,unsigned percentile)const{
        std::uint64_t values[window_frames];unsigned count=0;
        for(unsigned i=0;i<size;++i)if(!samples[i].invalid)values[count++]=samples[i].*field;
        if(!count)return 0;
        std::sort(values,values+count);return values[(count-1)*percentile/100];
    }
    Sample sum()const{Sample s;for(unsigned i=0;i<size;++i){const auto& x=samples[i];
        if(!x.invalid){s.query+=x.query;s.descent+=x.descent;s.non_descent+=x.non_descent;}
        s.queries+=x.queries;s.descents+=x.descents;s.invalid+=x.invalid;
        s.misses+=x.misses;s.verifies+=x.verifies;s.ineligible+=x.ineligible;s.visits+=x.visits;s.triangles+=x.triangles;s.contacts+=x.contacts;
        s.clock+=x.clock;s.reentry+=x.reentry;s.foreign+=x.foreign;s.unwind+=x.unwind;s.reset+=x.reset;s.cpu_mode+=x.cpu_mode;
    }return s;}
};
}
