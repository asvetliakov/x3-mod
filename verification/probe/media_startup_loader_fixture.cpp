// Host doubles execute the production loader delegate extracted unchanged.
#include <cstdint>
#include <cstdio>
#include <cstring>
#define WINAPI
using UINT=unsigned;using DWORD=std::uint32_t;using HRESULT=std::int32_t;
#define SUCCEEDED(hr) ((hr)>=0)
struct IDirect3D9 {unsigned identity;};
static IDirect3D9 native_factory{1},wrapped_factory{2};
static bool ownership_enabled=false,depth_copy_enabled=false,finite_positions_enabled=false,
    lock_bookends_enabled=false,locked_prefix_enabled=false;
static unsigned calls=0,hooks=0,wraps=0,vetoes=0,before=0,after=0,scopes=0,logs=0;
static bool backend_present=true,backend_success=true,wrap_success=true,telemetry_on=true,trace_on=false;
static IDirect3D9* hooked=nullptr;static UINT seen_sdk=0;
namespace x3m {
struct CpuCallBoundary {void before_original(){++before;}void after_original(){++after;}};
void log(const char*,...){++logs;}
void hook_direct3d(IDirect3D9* value){++hooks;hooked=value;}
namespace telemetry {unsigned now(){static unsigned tick=0;return ++tick;}bool enabled(){return telemetry_on;}}
namespace object_trace {bool active(){return trace_on;}}
namespace ownership {
struct Options {bool capture_auto_depth=false,track_buffer_writes=false,track_buffer_lock_attempts=false,capture_finite_positions=false,locked_prefix_bounds=false,track_execution_state=true;};
Options options;
void* process_admission_monitor(){return nullptr;}
struct ApplicationAdmissionAbi {explicit ApplicationAdmissionAbi(void*){++scopes;}~ApplicationAdmissionAbi(){--scopes;}};
enum class AdmissionVeto {UnobservedRoute};
void admission_veto(void*,AdmissionVeto){++vetoes;}
HRESULT wrap_factory(IDirect3D9*,IDirect3D9** out,const Options& value){++wraps;options=value;if(!wrap_success)return -1;*out=&wrapped_factory;return 0;}
}
}
unsigned GetCurrentThreadId(){return 7;}
static IDirect3D9* native(UINT sdk){++calls;seen_sdk=sdk;return backend_success?&native_factory:nullptr;}
static void* entry(const char* name){return !std::strcmp(name,"Direct3DCreate9")&&backend_present?reinterpret_cast<void*>(&native):nullptr;}
#include "media_startup_loader_under_test_inc.h"
static unsigned checks=0,failures=0;
static void check(bool b){++checks;if(!b)++failures;}
int main(){
    for(unsigned scenario=0;scenario<6;++scenario){
        calls=hooks=wraps=vetoes=before=after=scopes=logs=0;hooked=nullptr;seen_sdk=0;
        backend_present=scenario!=0;backend_success=scenario!=1;ownership_enabled=scenario>=3;wrap_success=scenario!=4;
        depth_copy_enabled=finite_positions_enabled=lock_bookends_enabled=locked_prefix_enabled=scenario==5;
        const auto result=x3m_direct3d_create9_body(0x20);
        check(scopes==0&&before==1&&after==1);check(calls==unsigned(backend_present));
        check(!calls||seen_sdk==0x20);
        if(scenario<2){check(!result&&!hooks&&!wraps&&!vetoes);}
        else {check(hooks==1&&hooked==result);check(wraps==unsigned(ownership_enabled));
            check(result==((ownership_enabled&&wrap_success)?&wrapped_factory:&native_factory));
            check(vetoes==unsigned(!ownership_enabled||!wrap_success));}
        if(scenario==5){const auto& o=x3m::ownership::options;check(o.capture_auto_depth&&o.track_buffer_writes&&o.track_buffer_lock_attempts&&o.capture_finite_positions&&o.locked_prefix_bounds&&!o.track_execution_state);}
    }
    std::printf("MEDIA STARTUP LOADER checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
