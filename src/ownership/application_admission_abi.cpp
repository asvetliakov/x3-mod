#include "application_admission_abi.h"
#include <windows.h>
#include <atomic>

#ifdef __EXCEPTIONS
#error "Compile only this ABI adapter translation unit with -fno-exceptions; keep the core exception-enabled."
#endif

namespace x3m::ownership {
namespace {
struct AdmissionState {
    unsigned char x87[108];
    unsigned mxcsr;
    DWORD error;
    AdmissionState(){
        // Opaque transport only: no x87 arithmetic and no long-lived save across
        // the application's native operation. Immediate FRSTOR keeps live ST.
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1"
                     : "=m"(x87),"=m"(mxcsr) :: "memory");
        error=GetLastError();
    }
    void restore() const {
        SetLastError(error);
        asm volatile("frstor %0\n\tldmxcsr %1" :: "m"(x87),"m"(mxcsr) : "memory");
    }
};

// One process/DLL-lifetime monitor; never replace it while objects or scopes
// survive. No graphics work or admission wait occurs in its configuration.
AdmissionMonitor process_monitor;
INIT_ONCE process_configuration = INIT_ONCE_STATIC_INIT;
std::atomic<bool> process_configured{false};
AdmissionMonitor* selected_process_monitor=nullptr;
BOOL CALLBACK configure_process_admission(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t setting[2]{};
    if(GetEnvironmentVariableW(L"X3M_ADMISSION",setting,2)==1 && setting[0]==L'1')
        selected_process_monitor=&process_monitor;
    process_configured.store(true,std::memory_order_release);
    return TRUE;
}
}
AdmissionMonitor* process_admission_monitor() noexcept {
    if(process_configured.load(std::memory_order_acquire))return selected_process_monitor;
    AdmissionState state;
    InitOnceExecuteOnce(&process_configuration,configure_process_admission,nullptr,nullptr);
    auto* result=process_configured.load(std::memory_order_acquire)?selected_process_monitor:nullptr;
    state.restore();return result;
}
ApplicationAdmissionAbi::ApplicationAdmissionAbi(AdmissionMonitor* monitor):requested_(monitor!=nullptr){
    if(!monitor)return;
    AdmissionState state;
    thread_=std::this_thread::get_id();
    new(storage_) ApplicationAdmission(*monitor);present_=true;result_=core()->result();
    state.restore();
}
ApplicationAdmissionAbi::~ApplicationAdmissionAbi(){
    if(!present_)return;
    AdmissionState state;core()->~ApplicationAdmission();present_=false;state.restore();
}
bool ApplicationAdmissionAbi::finish(){
    if(!requested_)return true;
    AdmissionState state;bool finished=false;
    if(thread_==std::this_thread::get_id()){
        finished=!present_||core()->finish();
        if(finished&&present_){core()->~ApplicationAdmission();present_=false;}
    }
    state.restore();return finished;
}
ApplicationAdmission* ApplicationAdmissionAbi::boundary() noexcept {
    if(!requested_)return nullptr;
    AdmissionState state;ApplicationAdmission* result=nullptr;
    if(thread_==std::this_thread::get_id()&&admitted())result=core();
    state.restore();return result;
}
ReplayAdmissionAbi::ReplayAdmissionAbi(ApplicationAdmissionAbi& application)
    :ReplayAdmissionAbi(application.boundary()){}
ReplayAdmissionAbi::ReplayAdmissionAbi(ApplicationAdmission* boundary):requested_(boundary!=nullptr){
    if(!boundary)return;
    AdmissionState state;thread_=std::this_thread::get_id();
    new(storage_) ReplayAdmission(*boundary);present_=true;result_=core()->result();
    state.restore();
}
ReplayAdmissionAbi::~ReplayAdmissionAbi(){
    if(!present_)return;
    AdmissionState state;core()->~ReplayAdmission();present_=false;state.restore();
}
bool ReplayAdmissionAbi::finish(){
    if(!requested_)return true;
    AdmissionState state;bool finished=false;
    if(thread_==std::this_thread::get_id()){
        finished=!present_||core()->finish();
        if(finished&&present_){core()->~ReplayAdmission();present_=false;}
    }
    state.restore();return finished;
}
void admission_veto(AdmissionMonitor* monitor,AdmissionVeto reason){
    if(!monitor||reason==AdmissionVeto::None)return;
    AdmissionState state;monitor->veto(reason);state.restore();
}
AdmissionSnapshot admission_snapshot(const AdmissionMonitor* monitor){
    if(!monitor)return {};
    AdmissionState state;const auto result=monitor->snapshot();state.restore();return result;
}
}
