// Tiny standalone native API/registration witness. No D3D factory or game.
#include "../../src/proxy/media_destination.h"
#include "../../src/proxy/media_cue.h"
#include "../../src/proxy/media_cue_sites.h"
#include "../../src/ownership/d3d9_ownership.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <thread>
namespace d=x3m::media_destination;namespace o=x3m::ownership;namespace cue=x3m::media_cue;
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u %s\n",__LINE__,#x);}}while(0)
namespace x3m {void log(const char*,...){}HANDLE log_handle(){return INVALID_HANDLE_VALUE;}}
namespace x3m::telemetry {bool enabled(){return false;}}
namespace x3m::object_trace {bool executable_verified(){return true;}}
__attribute__((section(".x3map"),used)) unsigned char fixture_map[0x21f000]{};
static bool own_map(){
    auto* module=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(reinterpret_cast<std::uintptr_t>(module)!=0x400000||reinterpret_cast<std::uintptr_t>(fixture_map)!=0x401000)return false;
    MEMORY_BASIC_INFORMATION info{};const unsigned address=cue::sites::kSites[0].address;
    if(VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info)!=sizeof info||info.State!=MEM_COMMIT||info.Type!=MEM_IMAGE||info.AllocationBase!=module)return false;
    const auto begin=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if(begin<0x401000||begin+info.RegionSize>0x620000||address<begin||address+16>begin+info.RegionSize)return false;
    DWORD previous=0;return VirtualProtect(reinterpret_cast<void*>(address),16,PAGE_EXECUTE_READWRITE,&previous)!=FALSE;
}
static bool eligible(unsigned,unsigned) noexcept{return true;}
static bool other(unsigned,unsigned) noexcept{return true;}
static void foreign_reset(const o::ResetEvent&) noexcept{}
struct Identity:d::IdentitySource {bool snapshot(unsigned,unsigned,o::SurfaceLeaseIdentity&) noexcept override{return false;}};
struct Memory:d::Memory {bool read(unsigned,void*,unsigned) noexcept override{return false;}bool write(unsigned,const void*,unsigned) noexcept override{return false;}};
int main(int argc,char** argv){
    if(argc!=2)return 2;
    if(!std::strncmp(argv[1],"cue-",4)){
        CHECK(own_map());if(failures)return 1;
        const auto& spec=cue::sites::kSites[0];std::memcpy(reinterpret_cast<void*>(spec.address),spec.expected,spec.length);
        const bool installed=!std::strcmp(argv[1],"cue-installed");
        SetEnvironmentVariableW(L"X3M_MEDIA_CUE_TRACE",nullptr);SetEnvironmentVariableW(L"X3M_MEDIA_CUE_CACHE",installed?L"1":nullptr);
        CHECK(cue::initialize()==installed);
        CHECK(cue::composition()==(installed?cue::Composition::installed_qualified:cue::Composition::disabled_pristine));
        unsigned char before[8];std::memcpy(before,reinterpret_cast<void*>(spec.address),8);
        CHECK(cue::set_owned_eligibility(eligible));CHECK(cue::set_owned_eligibility(eligible));CHECK(!cue::set_owned_eligibility(other));
        CHECK(cue::owned_eligibility_is(eligible));CHECK(!std::memcmp(before,reinterpret_cast<void*>(spec.address),8));
        CHECK(!cue::clear_owned_eligibility(other));CHECK(cue::clear_owned_eligibility(eligible));
        cue::Composition foreign=cue::Composition::disabled_pristine;std::thread thread([&]{foreign=cue::composition();});thread.join();CHECK(foreign==cue::Composition::unavailable);
        DWORD previous=0;CHECK(VirtualProtect(reinterpret_cast<void*>(spec.address),8,PAGE_EXECUTE_READWRITE,&previous));
        *reinterpret_cast<unsigned char*>(spec.address)^=0x11;CHECK(cue::composition()==cue::Composition::unavailable);
    }else{
        static x3m::media_presentation_gate::Admission gate;static Identity identity;static Memory memory;static d::Routes routes;
        static d::Destination first(gate,identity,GetCurrentThreadId()),second(gate,identity,GetCurrentThreadId());
        static d::Observer first_observer(first,memory,routes,{}),second_observer(second,memory,routes,{});
        const bool conflict=!std::strcmp(argv[1],"reset-conflict");
        CHECK(!o::claim_reset_observer(nullptr));CHECK(!d::bind_reset_observer(nullptr));
        if(conflict)CHECK(o::claim_reset_observer(foreign_reset));
        CHECK(d::bind_reset_observer(&first)==!conflict);CHECK(d::bind_reset_observer(&first)==!conflict);
        CHECK(!d::bind_reset_observer(&second));CHECK(d::reset_observer_bound(&first)==!conflict);CHECK(!d::reset_observer_bound(&second));
        CHECK(o::claim_reset_observer(foreign_reset)==conflict);CHECK(o::reset_observer_is(foreign_reset)==conflict);
        CHECK(!d::bind_dispatcher(nullptr));CHECK(d::bind_dispatcher(&first_observer));CHECK(d::bind_dispatcher(&first_observer));CHECK(!d::bind_dispatcher(&second_observer));
    }
    std::printf("mode=%s checks=%u failures=%u\n",argv[1],checks,failures);return failures?1:0;
}
