// Standalone recorder lifecycle and paired enabled/disabled overhead. Never X3.
#include "../../src/proxy/loading_trace_light.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>
namespace light=x3m::loading_trace::light;
namespace intervals=x3m::loading_trace::intervals;
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
void call(){light::Span s;s.begin(0);s.before_call();s.finish();}
void cpu_call(){
    light::Span span;
    alignas(16) unsigned char saved[108],before[108],after[108];
    uint32_t before_mxcsr=0,after_mxcsr=0;
    asm volatile("fnsave %0\n\tfninit\n\tfld1\n\tfldpi\n\tfnsave %1\n\tfrstor %1\n\tstmxcsr %2"
                 : "=m"(saved),"=m"(before),"=m"(before_mxcsr)::"memory");
    SetLastError(0x1234);span.begin(0);span.before_call();const DWORD caller=GetLastError();
    SetLastError(0x5678);span.finish();const DWORD callee=GetLastError();
    asm volatile("fnsave %0\n\tfrstor %2\n\tstmxcsr %1" : "=m"(after),"=m"(after_mxcsr):"m"(saved):"memory");
    check(caller==0x1234&&callee==0x5678,"CPU fixture LastError");
    check(!std::memcmp(before,after,sizeof(before))&&before_mxcsr==after_mxcsr,"live x87 and MXCSR unchanged");
}
DWORD WINAPI thread_call(void*){call();return 0;}
int main(int argc,char** argv){
    const char* mode=argc>1?argv[1]:"normal";
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);light::initialize();
    unsigned fault=0;
    if(!std::strcmp(mode,"allocation"))fault=1;
    if(!std::strcmp(mode,"tls_alloc"))fault=2;
    if(!std::strcmp(mode,"tls_set"))fault=4;
    if(!std::strcmp(mode,"reuse"))fault=8;
    light::fixture_interval_failures(fault);
    const bool disabled=!std::strcmp(mode,"bench_disabled");
    light::intervals_initialize(!disabled,uint64_t(f.QuadPart));
    const auto start=light::tick();
    if(!std::strncmp(mode,"bench_",6)){
        const unsigned threads=argc>2?unsigned(std::strtoul(argv[2],nullptr,10)):1;
        const unsigned nesting=argc>3?unsigned(std::strtoul(argv[3],nullptr,10)):1;
        if((threads!=1&&threads!=4)||(nesting!=1&&nesting!=2))return 2;
        constexpr unsigned calls=100000;std::vector<std::thread> workers;
        for(unsigned i=0;i<threads;++i)workers.emplace_back([&]{for(unsigned n=0;n<calls;++n){light::Span outer;outer.begin(0);outer.before_call();if(nesting==2)call();outer.finish();}});
        for(auto& t:workers)t.join();
        const auto end=light::tick();
        std::printf("interval_bench mode=%s threads=%u nesting=%u calls=%u ticks=%llu frequency=%llu ns_per_span=%.3f includes=QPC_TLS_counters_admission_ring thread_start_included=1\n",mode,threads,nesting,calls*threads*nesting,end-start,uint64_t(f.QuadPart),double(end-start)*1e9/double(f.QuadPart)/double(calls*threads*nesting));
        return 0;
    }
    if(!std::strcmp(mode,"threads")||fault==8){
        const unsigned count=fault==8?2:17;
        for(unsigned i=0;i<count;++i){HANDLE t=CreateThread(nullptr,0,thread_call,nullptr,0,nullptr);check(t!=nullptr,"create thread");if(t){WaitForSingleObject(t,INFINITE);CloseHandle(t);}}
    }else if(!std::strcmp(mode,"abandon")){
        // Unwind skips explicit finish: no payload can be inspected afterwards.
        try {light::Span token;token.begin(0);throw 1;}catch(int){}
    }else{
        if(!fault){cpu_call();cpu_call();}
        SetLastError(0x1234);light::Span outer;outer.begin(0);outer.before_call();check(GetLastError()==0x1234,"caller error");
        light::Span inner;inner.begin(1);inner.before_call();SetLastError(0x5678);inner.finish();check(GetLastError()==0x5678,"callee error");
        const auto end=light::tick();light::intervals_freeze(start,end,3,4,5,GetCurrentThreadId());
        const intervals::Header* h=nullptr;const intervals::Ring* r=nullptr;const intervals::Record* records=nullptr;LONG active=0;
        if(!fault)check(light::intervals_snapshot(h,r,records,active)==2&&!h&&!r&&!records&&active==1,"frozen active no payload");
        outer.finish();
    }
    light::intervals_freeze(start,light::tick(),3,4,5,GetCurrentThreadId());
    const intervals::Header* h=nullptr;const intervals::Ring* r=nullptr;const intervals::Record* records=nullptr;LONG active=0;
    const unsigned state=light::intervals_snapshot(h,r,records,active);
    if(!std::strcmp(mode,"abandon")){check(state==2&&active==1&&!h&&!r&&!records,"abandoned not readable");}
    else{
        check(state==3&&h&&r,"drained snapshot");
        if(state==3){
            if(fault==1)check(h->flags&intervals::Allocation,"allocation flag");
            else if(fault==2||fault==4)check(h->flags&intervals::Tls,"TLS flag");
            else if(!std::strcmp(mode,"threads"))check((h->flags&intervals::Threads)&&h->registered==16,"seventeenth thread rejected");
            else if(fault==8)check(r[0].tid==42&&r[1].tid==42&&r[0].generation!=r[1].generation,"TID reuse distinct slots");
            else check(!h->flags&&r[0].count==4&&records[2].operation==1&&records[3].operation==0&&records[3].end>=h->end,"nested finish after cutoff");
        }
        check(light::intervals_snapshot(h,r,records,active)==0,"once only");
    }
    std::printf("loading_intervals_fixture mode=%s checks=%u failures=%u\n",mode,checks,failures);
    return failures?1:0;
}
