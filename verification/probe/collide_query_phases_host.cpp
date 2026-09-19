#include "../../src/proxy/collide_query_phases_core.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <chrono>
static bool forbid=false;
void* operator new(std::size_t n){if(forbid)std::abort();if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
using namespace x3m::collide_query_phases::core;
static unsigned checks=0;
static void check(bool okay){++checks;if(!okay){std::fprintf(stderr,"FAIL %u\n",checks);std::exit(1);}}
int main(){
    forbid=true;Accumulator a;Window w;
    a.begin(100);a.enter(110);a.leave(150);a.end(180);
    auto s=a.take();check(s.query==80 && s.descent==40 && s.non_descent==40 && s.queries==1 && s.descents==1 && !s.invalid);
    a.begin(200);a.end(220);s=a.take();check(s.query==20 && !s.descent && s.non_descent==20 && s.queries==1);
    for(unsigned fault=0;fault<8;++fault){
        a.begin(fault==0?0:100);
        if(fault==1)a.invalidate();
        if(fault==2)a.begin(105);
        if(fault==3){a.enter(110);a.enter(115);a.leave(120);a.leave(125);}
        if(fault==4)a.enter(110);
        if(fault==5){a.enter(120);a.leave(110);}
        if(fault==6){a.enter(110);a.leave(500);}
        if(fault==7)a.leave(120);
        a.end(200);s=a.take();check(s.invalid==1 && !s.queries && !s.query && !s.descent && !s.non_descent);
    }
    a.begin(100);a.end(0);s=a.take();check(s.invalid==1 && !s.queries);
    a.begin(100);a.end(90);s=a.take();check(s.invalid==1 && !s.queries);
    a.begin(100);a.enter(110);s=a.take();check(s.invalid==1 && !a.pending && !a.depth);
    a.begin(100);a.end(150);a={};s=a.take();check(!s.query && !s.queries); // reset discards all accumulated work
    for(unsigned i=0;i<300;++i){s={};s.query=300+i;s.descent=i%2?i:300+i;s.non_descent=s.query-s.descent;s.queries=1;check(w.add(s));}
    check(!w.add(s));check(w.quantile(&Sample::non_descent,50)==0);
    check(w.quantile(&Sample::non_descent,95)==300);
    // Subtracting independent quantiles would be wrong for this witness.
    check(w.quantile(&Sample::query,50)-w.quantile(&Sample::descent,50)!=w.quantile(&Sample::non_descent,50));
    s=w.sum();check(s.queries==300 && s.non_descent==45000 && s.query==s.descent+s.non_descent);
    Window poisoned;Sample valid{};valid.query=100;valid.descent=60;valid.non_descent=40;
    poisoned.add(valid);Sample partial=valid;partial.query=1;partial.invalid=1;poisoned.add(partial);
    check(poisoned.valid_frames()==1 && poisoned.quantile(&Sample::query,50)==100 && poisoned.sum().query==100);
    Accumulator reasons;reasons.begin(1,Verify);reasons.invalidate(Foreign);reasons.end(5,10,2,1);
    check(reasons.frame.verifies==1 && reasons.frame.foreign==1 && reasons.frame.visits==10 && reasons.frame.triangles==2 && reasons.frame.contacts==1);
    volatile std::uint64_t sink=0;constexpr unsigned count=1000000;
    const auto begin=std::chrono::steady_clock::now();
    for(unsigned i=0;i<count;++i){a.begin(i+1);a.enter(i+2);a.leave(i+3);a.end(i+4);if((i&255)==255)sink+=a.take().query;}
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    std::printf("COLLIDE QUERY HOST checks=%u failures=0 accumulator_ns=%.2f iterations=%u witness=%llu\n",checks,double(ns)/count,count,static_cast<unsigned long long>(sink));
}
