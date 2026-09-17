#include "sun_light_poll.h"
#include "engine_memory.h"
#include "object_trace.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*)==4,"Verified x86 image layout only");
namespace x3m::sun_light_poll {
namespace {
constexpr std::uintptr_t context_slot_address=0x00608518;
constexpr std::uint32_t array_offset=0x5e8c,array_entries=255,slot_count_index=255; // R+0x6288 is the dword after the 255-entry array
constexpr std::uint32_t node_block=0xb0,node_block_size=0xc0;                       // +0xb0 .. +0x16f
std::uintptr_t context_slot=0;
bool active=false;
Status state=Status::Disabled;
std::uint32_t word(const unsigned char* block,std::uint32_t offset){std::uint32_t v=0;std::memcpy(&v,block+(offset-node_block),4);return v;}
std::int32_t colour(const unsigned char* block,std::uint32_t offset){std::int16_t v=0;std::memcpy(&v,block+(offset-node_block),2);return v;}
bool flag(const wchar_t* name,bool fallback) {
    wchar_t setting[4]{};
    const DWORD n=GetEnvironmentVariableW(name,setting,4);
    return n==1?setting[0]!=L'0':n==0?fallback:true;
}
}
const char* status_name(Status status) {
    switch(status){
    case Status::Ok:return "ok";case Status::Disabled:return "disabled";case Status::ExecutableMismatch:return "executable_mismatch";
    case Status::SlotUnreadable:return "slot_unreadable";case Status::NullContext:return "null_context";case Status::Unreadable:return "unreadable";
    case Status::Layout:return "layout";case Status::NoDirectional:return "no_directional";
    }
    return "?";
}
bool initialize() {
    if(active)return true;
    const DWORD error=GetLastError();
    wchar_t list[8]{};
    const DWORD cascades=GetEnvironmentVariableW(L"X3M_SHADOW_CASCADES",list,8); // a longer list reports its length: set
    const bool requested=flag(L"X3M_MOTION_OUTPUT",false)&&flag(L"X3M_SHADOW_REPLAY_DEPTH",false)&&cascades!=0&&!(cascades==1&&list[0]==L'0')&&flag(L"X3M_SHADOW_SUN_POLL",true);
    if(!requested){state=Status::Disabled;SetLastError(error);return false;}
    if(!object_trace::executable_verified()){state=Status::ExecutableMismatch;SetLastError(error);return false;}
    std::uint32_t context=0;
    if(!engine_memory::read(context_slot_address,&context,4)){state=Status::SlotUnreadable;SetLastError(error);return false;}
    context_slot=context_slot_address;active=true;state=Status::Ok;SetLastError(error);return true;
}
bool available(){return active;}
const char* status(){return status_name(state);}
bool read(Sample* out) {
    if(!out)return false;
    *out=Sample{};
    if(!active){out->status=state;return false;}
    const DWORD error=GetLastError();
    const auto finish=[&](Status s){out->status=s;SetLastError(error);return s==Status::Ok;};
    std::uint32_t context=0;
    if(!engine_memory::read(context_slot,&context,4))return finish(Status::Unreadable);
    if(!context)return finish(Status::NullContext);
    std::uint32_t entries[array_entries+1]{};
    if(context>0xffffffffu-array_offset||!engine_memory::read(context+array_offset,entries,sizeof entries))return finish(Status::Unreadable);
    if(entries[slot_count_index]!=8)return finish(Status::Layout);
    std::uint32_t count=0;
    while(count<array_entries&&entries[count])++count;
    if(count==array_entries)return finish(Status::Layout); // no terminator
    out->candidates=count;
    bool found=false;std::uint64_t best_reach=0;std::uint32_t best_record=0;
    for(std::uint32_t i=0;i<count;++i){
        unsigned char block[node_block_size];
        if(entries[i]>0xffffffffu-node_block-node_block_size||!engine_memory::read(entries[i]+node_block,block,sizeof block))return finish(Status::Unreadable);
        const std::uint32_t flags=word(block,0x12c);
        if(!(flags&4u))return finish(Status::Layout); // 0x0047c640 admits lights only
        if(flags&0x400010u)continue;                  // point or spot
        ++out->directional;
        const std::int32_t r=colour(block,0x150),g=colour(block,0x152),b=colour(block,0x154);
        const std::int32_t weighted=299*r+587*g+114*b;
        const std::uint32_t luma=weighted>0?std::uint32_t(weighted):0u;
        std::int32_t position[3];std::memcpy(position,block,12);
        std::uint64_t reach=0;
        for(std::int32_t p:position){const std::uint64_t a=p<0?std::uint64_t(-std::int64_t(p)):std::uint64_t(p);if(a>reach)reach=a;}
        const bool better=!found||luma>out->luma1000||(luma==out->luma1000&&reach>best_reach);
        if(better){
            if(found)out->second_luma1000=out->luma1000;
            found=true;out->luma1000=luma;best_reach=reach;out->flags=flags;best_record=word(block,0x16c);
            std::memcpy(out->position,position,12);
        } else if(luma>out->second_luma1000)out->second_luma1000=luma;
    }
    if(!found)return finish(Status::NoDirectional);
    if(best_record&&best_record<=0xffffffffu-0x40u&&engine_memory::read(best_record+0x34,out->record_position,12))out->record_valid=true;
    return finish(Status::Ok);
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_install(const std::uint32_t* slot) {
    context_slot=reinterpret_cast<std::uintptr_t>(slot);
    active=context_slot!=0;state=active?Status::Ok:Status::Disabled;
    engine_memory::reset();
}
#endif
}
