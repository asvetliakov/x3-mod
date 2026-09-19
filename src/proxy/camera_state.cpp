#include "cull_small_parts_core.h"
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
bool extra_consumer=false; // request_consumer(): an option the DLL's parser accepted
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
    // The route's temporal consumer, or the shadow-replay counter/depth replay
    // (docs/architecture/shadow-replay-gates.md, W1: the slice-0 test and the
    // cascade-0 projection need the scene camera latch without TAA).
    const bool motion=GetEnvironmentVariableW(L"X3M_MOTION_OUTPUT",setting,4)==1&&setting[0]==L'1';
    const bool consumer=(GetEnvironmentVariableW(L"X3M_TAA",setting,4)==1&&setting[0]==L'1')||
        (GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_CANDIDATES",setting,4)==1&&setting[0]==L'1')||
        (GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_DEPTH",setting,4)==1&&setting[0]==L'1')||
        // The light-map far fade (P[0] for its per-draw footprint): armed by
        // capture.cpp only after its parser accepted the value (request_consumer).
        extra_consumer;
    // The small-parts cull (X3M_CULL_SMALL_PARTS_PX, cull_small_parts.cpp)
    // reads P[0] once per frame for its pixel threshold: same read-only latch.
    wchar_t px[16]{};
    const DWORD px_length=GetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX",px,16);
    // Armed only for a value the DLL's own parser accepts (in band, > 0).
    char px_text[16]{};
    bool px_ascii=px_length>=1&&px_length<16;
    for(DWORD i=0;px_ascii&&i<px_length;++i){px_ascii=px[i]>=0x21&&px[i]<=0x7e;px_text[i]=static_cast<char>(px[i]);}
    double px_value=0;
    const bool small_parts=px_ascii&&cull_small_parts::core::parse_px(px_text,&px_value)&&cull_small_parts::core::valid_px(px_value);
    const bool requested=(motion&&consumer)||small_parts;
    if(!requested){state="disabled";SetLastError(error);return false;}
    if(!object_trace::executable_verified()){state="executable_mismatch";SetLastError(error);return false;}
    const uintptr_t base=0x400000;
    projection_slot=base+0x208a38;view_slot=base+0x208a40;
    if(!readable(projection_slot,4)||!readable(view_slot,4)){state="slots_unreadable";projection_slot=view_slot=0;SetLastError(error);return false;}
    active=true;state="active";SetLastError(error);return true;
}
bool available(){return active;}
const char* status(){return state;}
bool request_consumer(){extra_consumer=true;return initialize();}
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
