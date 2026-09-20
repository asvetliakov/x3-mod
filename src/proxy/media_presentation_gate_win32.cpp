#include "media_presentation_gate_win32.h"
#include "engine_patch.h"
#include "object_trace.h"
#include <windows.h>
#include <cstring>
#include <new>
namespace x3m::media_presentation_gate {
static_assert(sizeof(void*)==4,"x86 gate only");
namespace {
bool region(std::uint32_t address,unsigned size,MEMORY_BASIC_INFORMATION& info){
    return address&&size&&address<=0xffffffffu-size&&
        VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info)==sizeof info&&
        info.State==MEM_COMMIT&&!(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&
        std::uintptr_t(address)+size<=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
}
}
bool Win32Platform::qualified(){return object_trace::executable_verified();}
bool Win32Platform::install_window(){return engine_patch::install_window_open();}
std::uint32_t Win32Platform::reserve(){
    return reinterpret_cast<std::uint32_t>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
}
std::uint32_t Win32Platform::counter_address(const Admission& a){return reinterpret_cast<std::uint32_t>(a.depth_address());}
bool Win32Platform::read(std::uint32_t a,void* out,unsigned n){
    SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),out,n,&copied)&&copied==n;
}
bool Win32Platform::write_reserved(std::uint32_t a,const void* bytes,unsigned n){
    MEMORY_BASIC_INFORMATION info{};
    if(!region(a,n,info)||info.AllocationBase!=reinterpret_cast<void*>(a)||info.Type!=MEM_PRIVATE||info.Protect!=PAGE_READWRITE)return false;
    std::memcpy(reinterpret_cast<void*>(a),bytes,n);return true;
}
bool Win32Platform::executable(std::uint32_t a,unsigned n){
    MEMORY_BASIC_INFORMATION info{};
    if(!region(a,n,info))return false;
    return info.Protect==PAGE_EXECUTE_READ||info.Protect==PAGE_EXECUTE_READWRITE||info.Protect==PAGE_EXECUTE_WRITECOPY||info.Protect==PAGE_EXECUTE;
}
bool Win32Platform::protect(std::uint32_t a,unsigned n,std::uint32_t wanted,std::uint32_t& previous){
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(a),n,wanted,&old))return false;
    previous=old;
    // Success of VirtualProtect is the documented protection contract. The
    // transaction retains debt whenever this API reports failure.
    return true;
}
bool Win32Platform::compare8(std::uint32_t a,const unsigned char* expected,const unsigned char* desired){
    if(a&7)return false;
    LONGLONG before=0,after=0;std::memcpy(&before,expected,8);std::memcpy(&after,desired,8);
    return InterlockedCompareExchange64(reinterpret_cast<volatile LONGLONG*>(a),after,before)==before;
}
bool Win32Platform::flush(std::uint32_t a,unsigned n){return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(a),n)!=FALSE;}
Runtime* process_runtime(){
    static_assert(sizeof(Runtime)<=4096,"runtime allocation bound");
    static Runtime* runtime=nullptr;
    if(!runtime){void* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(p)runtime=new(p)Runtime;}
    return runtime;
}
}
