#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../src/ownership/application_admission_abi.h"
#include <cstdio>
#include <cstring>

using namespace x3m::ownership;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok)++failures;std::printf("CHECK %s %s\n",label,ok?"PASS":"FAIL");}
struct State {
    unsigned char fp[108];unsigned mxcsr;DWORD error;
    State(){asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1":"=m"(fp),"=m"(mxcsr)::"memory");error=GetLastError();}
    void restore()const{SetLastError(error);asm volatile("frstor %0\n\tldmxcsr %1"::"m"(fp),"m"(mxcsr):"memory");}
};
void seed(){
    const unsigned short cw=0x077f;const unsigned mx=0x3fa1;
    // Test-only arithmetic supplies live x87 values and masked sticky status.
    asm volatile("fninit\n\tfldz\n\tfldz\n\tfdivp\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");
    SetLastError(0x11223344);
}
bool same(const State& a,const State& b){return a.error==b.error&&a.mxcsr==b.mxcsr&&!std::memcmp(a.fp,b.fp,sizeof(a.fp));}
struct Worker {
    HANDLE start=nullptr;AdmissionMonitor* selected=nullptr;
    bool cpu_preserved=false,scope_matches=false;
};
DWORD WINAPI enter(void* raw){
    auto& worker=*static_cast<Worker*>(raw);
    if(WaitForSingleObject(worker.start,10000)!=WAIT_OBJECT_0)return 1;
    const State original;seed();const State before;
    worker.selected=process_admission_monitor();
    const State after;worker.cpu_preserved=same(before,after);original.restore();
    ApplicationAdmissionAbi admission(worker.selected);
    worker.scope_matches=admission.requested()==(worker.selected!=nullptr)&&
        admission.admitted()==(worker.selected!=nullptr);
    return 0;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const bool enabled=!std::strcmp(argv[1],"enabled");
    const wchar_t* setting=nullptr;
    if(enabled)setting=L"1";
    else if(!std::strcmp(argv[1],"malformed"))setting=L"01";
    else if(!std::strcmp(argv[1],"long"))setting=L"11111111";
    else if(std::strcmp(argv[1],"disabled"))return 2;
    check(SetEnvironmentVariableW(L"X3M_ADMISSION",setting)!=0,"initial setting");
    HANDLE start=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(start!=nullptr,"start barrier");
    if(!start)return 1;
    Worker workers[8]{};HANDLE threads[8]{};unsigned created=0;
    for(unsigned n=0;n<8;++n){workers[n].start=start;threads[n]=CreateThread(nullptr,0,enter,&workers[n],0,nullptr);check(threads[n]!=nullptr,"worker creation");if(!threads[n])break;++created;}
    check(SetEvent(start)!=0,"release first-access race");
    if(created!=8){if(created)WaitForMultipleObjects(created,threads,TRUE,15000);return 1;}
    const DWORD joined=WaitForMultipleObjects(8,threads,TRUE,15000);
    check(joined==WAIT_OBJECT_0,"workers finish");if(joined!=WAIT_OBJECT_0)return 1;
    for(unsigned n=0;n<8;++n){
        check(workers[n].cpu_preserved,"cold getter preserves complete CPU state");
        check((workers[n].selected!=nullptr)==enabled,"exact setting selects expected mode");
        check(workers[n].selected==workers[0].selected,"all threads share one published pointer");
        check(workers[n].scope_matches,"scope follows published mode");
        CloseHandle(threads[n]);
    }
    CloseHandle(start);
    const auto state=admission_snapshot(workers[0].selected);
    check(!state.active_roots&&!state.waiting_roots&&!state.replay_active,"all worker scopes retired");
    check(state.admitted_roots==(enabled?8u:0u),"exact enabled root count");
    check(SetEnvironmentVariableW(L"X3M_ADMISSION",enabled?L"0":L"1")!=0,"change setting after initialization");
    const State original;seed();const State before;auto* selected=process_admission_monitor();const State after;original.restore();
    check(same(before,after),"hot getter preserves complete CPU state");
    check(selected==workers[0].selected,"configuration remains immutable");
    std::printf("RESULT %s checks=%u failures=%u threads=8 enabled=%u\n",failures?"FAIL":"PASS",checks,failures,enabled);
    return failures?1:0;
}
