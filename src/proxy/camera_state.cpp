#include "camera_state.h"
#include "object_trace.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*)==4,"Verified x86 image layout only");
namespace x3m::camera_state {
namespace {
// Slot addresses (each holds a pointer to a 64-byte matrix buffer).
uintptr_t projection_slot=0,view_slot=0;
bool active=false;
const char* state="disabled";
struct Cache { uintptr_t pointer=0; bool valid=false; };
Cache caches[2];
bool readable(uintptr_t address,size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if(!address||VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof info)!=sizeof info)return false;
    if(info.State!=MEM_COMMIT||(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const DWORD readable_protection=PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY;
    if(!(info.Protect&readable_protection))return false;
    const uintptr_t region_end=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
    return address+size>address&&address+size<=region_end;
}
// Reads one 64-byte buffer through its slot: the slot was validated at
// initialize; the buffer pointer is re-validated whenever its value changes.
bool read_buffer(unsigned index,uintptr_t slot,float* out,uintptr_t* address,std::uint32_t* failure) {
    uintptr_t pointer=0;std::memcpy(&pointer,reinterpret_cast<const void*>(slot),sizeof pointer);
    *address=pointer;
    if(!pointer){*failure=NullPointer;return false;}
    Cache& cache=caches[index];
    if(cache.pointer!=pointer||!cache.valid){cache.pointer=pointer;cache.valid=readable(pointer,64);}
    if(!cache.valid){*failure=Unreadable;return false;}
    std::memcpy(out,reinterpret_cast<const void*>(pointer),64);
    return true;
}
}
bool initialize() {
    if(active)return true;
    const DWORD error=GetLastError();
    wchar_t setting[4]{};
    const bool requested=GetEnvironmentVariableW(L"X3M_MOTION_OUTPUT",setting,4)==1&&setting[0]==L'1'&&
        GetEnvironmentVariableW(L"X3M_TAA",setting,4)==1&&setting[0]==L'1';
    if(!requested){state="disabled";SetLastError(error);return false;}
    if(!object_trace::executable_verified()){state="executable_mismatch";SetLastError(error);return false;}
    const uintptr_t base=0x400000;
    projection_slot=base+0x208a38;view_slot=base+0x208a40;
    if(!readable(projection_slot,4)||!readable(view_slot,4)){state="slots_unreadable";projection_slot=view_slot=0;SetLastError(error);return false;}
    active=true;state="active";SetLastError(error);return true;
}
bool available(){return active;}
const char* status(){return state;}
void reset(){caches[0]=Cache{};caches[1]=Cache{};}
bool read(Sample* out) {
    if(!out)return false;
    *out=Sample{};
    if(!active){out->read_failure=Unavailable;return false;}
    const DWORD error=GetLastError();
    bool ok=read_buffer(0,projection_slot,out->projection_raw,&out->projection,&out->read_failure)&&
            read_buffer(1,view_slot,out->view_raw,&out->view,&out->read_failure);
    if(ok){
        ok=renderer::camera_state_from_matrices(out->projection_raw,out->view_raw,out->state,&out->failure);
        if(!ok)out->read_failure=Invalid;
    }
    SetLastError(error);
    return ok;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_install(const float* const* projection,const float* const* view) {
    projection_slot=reinterpret_cast<uintptr_t>(projection);view_slot=reinterpret_cast<uintptr_t>(view);
    reset();
    active=projection_slot&&view_slot;
    state=active?"fixture":"disabled";
}
#endif
}
