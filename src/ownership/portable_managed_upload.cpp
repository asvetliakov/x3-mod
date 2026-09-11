#include "portable_managed_upload.h"
#include <windows.h>
#include <cstring>
#include <limits>
#include <type_traits>

namespace x3m::ownership::portable_upload {
namespace {
struct PreserveState {
    DWORD error=GetLastError();
    unsigned char x87[108];
    DWORD mxcsr;
    PreserveState() noexcept {
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1"
                     : "=m"(x87),"=m"(mxcsr) :: "memory");
    }
    ~PreserveState(){
        asm volatile("frstor %0\n\tldmxcsr %1" :: "m"(x87),"m"(mxcsr) : "memory");
        SetLastError(error);
    }
};
template<class Buffer> bool inspect_impl(Buffer* native,BufferContract& out) noexcept {
    if(!native)return false;
    constexpr bool vertex=std::is_same_v<Buffer,IDirect3DVertexBuffer9>;
    std::conditional_t<vertex,D3DVERTEXBUFFER_DESC,D3DINDEXBUFFER_DESC> desc{};
    if(native->GetDesc(&desc)!=S_OK||desc.Pool!=D3DPOOL_MANAGED||
       (desc.Usage&(D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC))||!desc.Size||
       desc.Size>maximum_buffer_bytes||
       desc.Type!=(vertex?D3DRTYPE_VERTEXBUFFER:D3DRTYPE_INDEXBUFFER)||
       (vertex?desc.Format!=D3DFMT_VERTEXDATA:
           (desc.Format!=D3DFMT_INDEX16&&desc.Format!=D3DFMT_INDEX32)))return false;
    out.size=desc.Size;out.usage=desc.Usage;
    out.type=desc.Type;out.format=desc.Format;
    if constexpr(vertex)out.fvf=desc.FVF;
    return true;
}
bool same_impl(IDirect3DResource9* native,const BufferContract& before) noexcept {
    if(!native||native->GetType()!=before.type)return false;
    BufferContract now{};
    bool good=false;
    if(before.type==D3DRTYPE_VERTEXBUFFER)
        good=inspect_impl(static_cast<IDirect3DVertexBuffer9*>(native),now);
    else if(before.type==D3DRTYPE_INDEXBUFFER)
        good=inspect_impl(static_cast<IDirect3DIndexBuffer9*>(native),now);
    return good&&now.size==before.size&&now.usage==before.usage&&
        now.format==before.format&&now.fvf==before.fvf;
}
}
CreationPlan plan_creation(bool finite_capture,UINT bytes,DWORD usage,D3DPOOL pool,
                           const HANDLE* shared_handle) noexcept {
    CreationPlan result{usage,usage,false};
    if(finite_capture&&bytes&&bytes<=maximum_buffer_bytes&&pool==D3DPOOL_MANAGED&&
       !shared_handle&&!(usage&D3DUSAGE_DYNAMIC)&&(usage&D3DUSAGE_WRITEONLY)){
        result.native_usage=usage&~DWORD(D3DUSAGE_WRITEONLY);result.converted=true;
    }
    return result;
}
bool inspect(IDirect3DVertexBuffer9* native,BufferContract* out) noexcept {
    PreserveState preserve;if(!out)return false;*out={};return inspect_impl(native,*out);
}
bool inspect(IDirect3DIndexBuffer9* native,BufferContract* out) noexcept {
    PreserveState preserve;if(!out)return false;*out={};return inspect_impl(native,*out);
}
bool same_description(IDirect3DResource9* native,const BufferContract& before) noexcept {
    PreserveState preserve;return same_impl(native,before);
}
bool normalize_window(IDirect3DResource9* native,const BufferContract& contract,UINT offset,UINT size,DWORD flags,
                      const void* pointer,Window* out) noexcept {
    PreserveState preserve;if(!out)return false;*out={};
    if((flags!=0&&flags!=D3DLOCK_NOSYSLOCK)||!pointer||!contract.size||
       contract.size>maximum_buffer_bytes||(!size&&offset)||offset>contract.size)return false;
    const UINT length=size?size:contract.size;
    if(!length||length>contract.size-offset)return false;
    const auto address=reinterpret_cast<std::uintptr_t>(pointer);
    if(length-1>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    if(!same_impl(native,contract))return false;
    *out={offset,length};return true;
}
}
