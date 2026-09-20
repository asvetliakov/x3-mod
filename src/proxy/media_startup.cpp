#include "media_startup.h"
namespace x3m::media_startup {
bool Controller::configure(Bootstrap callback,void* context) noexcept {
    if(!callback||gate_.load()!=open)return false;
    std::uint32_t empty=0;if(!configuration_.compare_exchange_strong(empty,1))return false;
    callback_=callback;context_=context;configuration_.store(2);return true;
}
void Controller::block(Status reason) noexcept {
    std::uint32_t expected=open;
    if(gate_.compare_exchange_strong(expected,blocked))status_.store(reason);
}
void Controller::enter(Entry& entry,std::uintptr_t esp) noexcept {
    entry={};
    if(active_.exchange(true)){block(Status::reentered);return;}
    entry.owns_entry=true;
    if(gate_.load()!=open)return;
    if(!esp||(esp&3)||esp>0xffffffe7u||!platform_.read_entry(esp,entry.words))return;
    entry.matched=entry.words[0]==0x4d8494&&entry.words[1]==0x20&&entry.words[5]==0x402ee1;
}
void Controller::leave(Entry& entry,std::uintptr_t result) noexcept {
    if(!entry.owns_entry)return;
    entry.owns_entry=false;
    // Remain active through identity validation and scheduling. A recursive
    // export before the request claim invalidates the outer attempt too.
    const auto finish=[&]() noexcept {
        if(!entry.matched||gate_.load()!=open)return;
        if(!result){block(Status::failed_factory);return;}
        if(configuration_.load()!=2){block(Status::unconfigured);return;}
        if(!platform_.qualified()){block(Status::identity_refused);return;}
        std::uint32_t expected=open;
        if(!gate_.compare_exchange_strong(expected,claimed))return;
        requested_.store(platform_.now());status_.store(Status::scheduled);
        if(!platform_.launch(*this))status_.store(Status::launch_failed);
    };
    finish();active_.store(false);
}
void Controller::device_creation_attempt() noexcept {
    std::uint32_t expected=open;
    if(gate_.compare_exchange_strong(expected,closed))status_.store(Status::closed);
    if(!device_seen_.exchange(true))device_.store(platform_.now());
}
void Controller::bootstrap(void* module) noexcept {
    Status expected=Status::scheduled;
    if(!status_.compare_exchange_strong(expected,Status::preparing))return;
    begin_.store(platform_.now());
    const bool okay=callback_(context_,BootstrapContext{module,requested_.load()});
    end_.store(platform_.now());
    if(!okay){status_.store(Status::callback_failed);return;}
    expected=Status::preparing;status_.compare_exchange_strong(expected,Status::prepared);
}
bool Controller::service_ready() noexcept {
    Status observed=status_.load();
    for(;;){
        if(observed!=Status::preparing&&observed!=Status::prepared)return false;
        if(status_.compare_exchange_weak(observed,Status::ready)){
            ready_.store(platform_.now());return true;
        }
    }
}
Snapshot Controller::snapshot() const noexcept {
    return {status_.load(),device_seen_.load(),gate_.load()==claimed,active_.load(),
        requested_.load(),begin_.load(),end_.load(),ready_.load(),device_.load()};
}
}

#ifdef _WIN32
#include "media_startup_abi.h"
#include "object_trace.h"
#include <windows.h>
#include <cstring>
static_assert(sizeof(void*)==4,"startup guard is the reviewed x86 export ABI");
namespace x3m::media_startup {
namespace {
// ReadProcessMemory closes the check-to-copy race. VirtualQuery is an explicit
// committed/readable/non-guard bound, not a promise that memory stays mapped.
bool safe_read(std::uintptr_t address,void* output,unsigned size) noexcept {
    if(!address||!size||address>0xffffffffu-size)return false;
    const auto end=address+size;
    for(auto cursor=address;cursor<end;){
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof info)!=sizeof info||
           info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           !(info.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(info.RegionSize>0xffffffffu-base)return false;
        const auto next=base+info.RegionSize;if(next<=cursor)return false;
        cursor=next<end?next:end;
    }
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&copied)&&copied==size;
}
bool anchor(std::uintptr_t address,const unsigned char* expected,unsigned count) noexcept {
    unsigned char actual[6]{};
    return count<=sizeof actual&&safe_read(address,actual,count)&&!std::memcmp(actual,expected,count);
}
class WindowsPlatform final:public Platform {
public:
    HMODULE module=nullptr;Controller* controller=nullptr;
    bool read_entry(std::uintptr_t address,std::uint32_t (&words)[6]) noexcept override {
        return safe_read(address,words,sizeof words);
    }
    bool qualified() noexcept override {
        static constexpr unsigned char setup[]={0xe8,0x8f,0x55,0x0d,0},sdk[]={0x6a,0x20},
            call[]={0xe8,0x48,0x2a,0x02,0},continuation[]={0x8b,0x0d,0x3c,0x8b,0x60,0},
            thunk[]={0xff,0x25,0x14,0x23,0x53,0};
        return object_trace::executable_verified()&&anchor(0x402edc,setup,sizeof setup)&&
            anchor(0x4d8488,sdk,sizeof sdk)&&anchor(0x4d848f,call,sizeof call)&&
            anchor(0x4d8494,continuation,sizeof continuation)&&anchor(0x4faedc,thunk,sizeof thunk);
    }
    std::uint64_t now() noexcept override {
        LARGE_INTEGER value{};return QueryPerformanceCounter(&value)?static_cast<std::uint64_t>(value.QuadPart):0;
    }
    static DWORD WINAPI worker(void* context) noexcept {
        auto& platform=*static_cast<WindowsPlatform*>(context);
        const HMODULE pin=platform.module;
        platform.controller->bootstrap(pin);
        // Canonical workers must own their own module references. No return
        // through proxy code after dropping this bootstrap's reference.
        FreeLibraryAndExitThread(pin,0);
    }
    bool launch(Controller& value) noexcept override {
        HMODULE pin=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&worker),&pin))return false;
        module=pin;controller=&value;
        HANDLE thread=CreateThread(nullptr,0,worker,this,0,nullptr);
        if(!thread){controller=nullptr;module=nullptr;FreeLibrary(pin);return false;}
        CloseHandle(thread);return true; // no join, wait, COM or window work
    }
};
WindowsPlatform platform;
Controller controller(platform);
}
Controller& process() noexcept {return controller;}
bool configure(Bootstrap callback,void* context) noexcept {return controller.configure(callback,context);}
bool service_ready() noexcept {return controller.service_ready();}
Snapshot snapshot() noexcept {return controller.snapshot();}
}
extern "C" {
extern const std::uint32_t x3m_media_startup_default_mxcsr=0x1f80;
__attribute__((force_align_arg_pointer)) void __cdecl x3m_media_startup_enter(
    x3m::media_startup::Entry* entry,std::uintptr_t esp) noexcept {
    const DWORD error=GetLastError();x3m::media_startup::process().enter(*entry,esp);SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_media_startup_leave(
    x3m::media_startup::Entry* entry,std::uintptr_t result) noexcept {
    const DWORD error=GetLastError();x3m::media_startup::process().leave(*entry,result);SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_media_startup_device_notify() noexcept {
    const DWORD error=GetLastError();x3m::media_startup::process().device_creation_attempt();SetLastError(error);
}
}
X3M_MEDIA_STARTUP_DEVICE_NOTIFY
#endif
