#include "../../src/proxy/loading_intervals_core.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
using namespace x3m::loading_trace::intervals;
struct Atomic {
    std::atomic<int32_t> value{0};
    int32_t read(){return value.load();}
    int32_t exchange(int32_t n){return value.exchange(n);}
    int32_t increment(){return value.fetch_add(1)+1;}
    int32_t decrement(){return value.fetch_sub(1)-1;}
    int32_t compare(int32_t n,int32_t expected){value.compare_exchange_strong(expected,n);return expected;}
};
unsigned checks=0,failures=0;
void check(bool ok){++checks;if(!ok)++failures;}
int main(){
    Record records[3]{};Ring ring;
    ring.append(records,3,10,20,1);ring.append(records,3,1,30,2); // nested return
    check(ring.count==2&&records[1].begin==1);
    ring.append(records,3,35,40,3);ring.append(records,3,45,50,4);
    check(ring.count==3&&ring.completed==4&&ring.lost_begin==10&&ring.lost_end==20);
    ring.append(records,3,55,60,4);check(ring.lost_begin==1&&ring.lost_end==30);
    ring.append(records,3,80,70,0);check(ring.flags&Clock);
    ring.flags=0;ring.append(records,3,10,59,0);check(ring.flags&Clock);
    ring.flags=0;ring.completed=UINT32_MAX;ring.append(records,3,90,100,0);
    check((ring.flags&Sequence)&&ring.completed==UINT32_MAX);
    Gate<Atomic> gate;check(!gate.enter());gate.enabled.exchange(1);
    check(gate.enter());check(gate.enter());gate.close();check(!gate.drained());
    gate.leave();check(!gate.drained());gate.leave();check(gate.drained());check(!gate.enter());
    // Exact freeze boundaries: close between initial read and increment, and
    // between increment and recheck. Neither rejected path accesses payload.
    for(unsigned boundary=0;boundary<2;++boundary){
        Gate<Atomic> g;g.enabled.exchange(1);check(g.enabled.read()==1);
        if(!boundary)g.close();g.active.increment();if(boundary)g.close();
        check(!g.recheck());check(g.drained());
    }
    Gate<Atomic> abandoned;abandoned.enabled.exchange(1);check(abandoned.enter());
    try {throw 1;}catch(int){} // explicit-finish token intentionally abandoned
    abandoned.close();check(!abandoned.drained()&&abandoned.active.read()==1);
    Gate<Atomic> saturated;saturated.enabled.exchange(1);saturated.active.exchange(INT32_MAX);
    check(!saturated.enter()&&saturated.active.read()==INT32_MAX);
    saturated.active.exchange(INT32_MAX-1);check(!saturated.enter());
    saturated.active.value.fetch_sub(INT32_MAX-1);saturated.close();check(!saturated.drained());
    // Concurrent accepted writers publish before leave; no reader until drain.
    for(unsigned repeat=0;repeat<100;++repeat){
        Gate<Atomic> g;g.enabled.exchange(1);std::atomic<unsigned> ready{0};
        unsigned payload[4]{};std::vector<std::thread> workers;
        for(unsigned i=0;i<4;++i)workers.emplace_back([&,i]{
            const bool admitted=g.enter();ready.fetch_add(1);
            while(g.enabled.read())std::this_thread::yield();
            if(admitted){payload[i]=i+1;g.leave();}
        });
        while(ready.load()!=4)std::this_thread::yield();g.close();
        while(!g.drained())std::this_thread::yield();
        check(payload[0]+payload[1]+payload[2]+payload[3]==10);
        for(auto& worker:workers)worker.join();
    }
    for(unsigned threads: {1u,4u}){
        Gate<Atomic> g;g.enabled.exchange(1);constexpr unsigned iterations=200000;
        std::vector<std::thread> workers;const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<threads;++i)workers.emplace_back([&]{for(unsigned n=0;n<iterations;++n)if(g.enter())g.leave();});
        for(auto& worker:workers)worker.join();
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        std::printf("host_gate threads=%u ns_per_call=%llu calls=%u\n",threads,(unsigned long long)(ns/(threads*iterations)),threads*iterations);
    }
    std::printf("loading_intervals_host checks=%u failures=%u payload_bytes=%u\n",checks,failures,slot_limit*capacity*unsigned(sizeof(Record)));
    return failures?1:0;
}
