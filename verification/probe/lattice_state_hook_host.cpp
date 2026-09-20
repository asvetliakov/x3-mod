// The actual production draw_indexed body is included by the Python test.
// Mock only the surrounding APIs; assert ordering, dispatch and CPU envelopes.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>
#include <initializer_list>
#define WINAPI
using HRESULT=int;using UINT=unsigned;using INT=int;using D3DPRIMITIVETYPE=unsigned;
#define FAILED(x) ((x)<0)
struct IDirect3DDevice9{} device;
static int incoming_fp=17,incoming_error=19,fp=incoming_fp,error=incoming_error,native_calls=0,scoped_calls=0;
static bool allow_submit=true,block=false;static HRESULT native_result=37;static std::vector<int> events;
static unsigned args_seen[7]{};
unsigned GetCurrentThreadId(){return 9;}
struct LightCallBoundary {int fi=fp,ei=error,fo=fp,eo=error;
    void before_original(){fp=fi;error=ei;}void after_original(){fo=fp;eo=error;}
    ~LightCallBoundary(){fp=fo;error=eo;}};
template<class F> void call_preserved(F&& f){++scoped_calls;int oldfp=fp,olde=error;f();fp=oldfp;error=olde;}
namespace ownership {int process_admission_monitor(){return 0;}struct ApplicationAdmissionAbi{explicit ApplicationAdmissionAbi(int){}};}
namespace frame_timing {enum class Bucket{Draw};void draw_native_begin(){events.push_back(4);}void draw_native_end(){events.push_back(6);}}
namespace telemetry {enum class Metric{DrawBackend};bool enabled(Metric){return false;}void record(int,Metric,int,bool){}}
struct PlainHookGuard {explicit PlainHookGuard(frame_timing::Bucket){}};
struct CallTimer {int backend_ticks=0;template<class T> CallTimer(T&,bool){}void begin(){}void end(){}};
namespace lattice_state {
using GetTarget=int(*)(IDirect3DDevice9*,unsigned,void**);
struct Arguments {unsigned topology,primitives,minimum,vertices,start;int base;};
struct Capture {int original_calls=0,effective_calls=0,result_calls=0;bool selected=true;
    bool can_select(const Arguments&)const{return true;}
    int original(IDirect3DDevice9*,const Arguments&,std::uint64_t){++original_calls;events.push_back(1);fp=88;error=99;return selected?0:-1;}
    template<class Route>void effective(int,IDirect3DDevice9* d,int,GetTarget get,const Route& r){
        if(!r.submit)return;++effective_calls;events.push_back(3);void* out=nullptr;get(d,0,&out);fp=188;error=199;
    }
    void result(int slot,HRESULT hr,bool submitted){if(slot<0)return;++result_calls;events.push_back(7);if(submitted&&hr!=native_result)std::abort();}
};}
struct Route {bool submit=true;HRESULT submission_error=-77;};
struct DrawCall {bool a,b;unsigned t,c,s;int base;unsigned m,n;bool composition=false;};
struct Motion {bool draw_submission_blocked(){return block;}bool composition_requested(){return false;}
    bool reference_accounting_busy(){return false;}void restore_bindings(){}
    Route before_draw(DrawCall){events.push_back(2);return {allow_submit,-77};}
    void after_draw(Route&,HRESULT){events.push_back(8);fp=788;error=799;}};
struct Depth{void before_draw(IDirect3DDevice9*,unsigned,unsigned){}void after_draw(HRESULT){}};
static int saved_getter_calls=0,logical_getter_calls=0;
static int saved_getter(IDirect3DDevice9*,unsigned,void**){++saved_getter_calls;return 0;}
static HRESULT original_draw(IDirect3DDevice9*,unsigned t,int b,unsigned m,unsigned n,unsigned s,unsigned c){
    ++native_calls;events.push_back(5);args_seen[0]=t;args_seen[1]=unsigned(b);args_seen[2]=m;args_seen[3]=n;args_seen[4]=s;args_seen[5]=c;
    if(fp!=incoming_fp||error!=incoming_error)std::abort();fp=41;error=43;return native_result;
}
struct Device {int stats=0,caps=0;Motion motion_output;Depth scene_depth;bool capture=false;
    bool composition_scene_owner=false,reset_active=false,compositor=false;unsigned scene_thread=9,bloom_busy=0,composition_draw_depth=0;
    std::uint64_t composition_scene_frame=0,frame=0,draws=0,id=1;std::shared_ptr<lattice_state::Capture> lattice_state;
    template<class F> F get(unsigned slot){return reinterpret_cast<F>(slot==38?reinterpret_cast<void*>(&saved_getter):reinterpret_cast<void*>(&original_draw));}
} ctx;
struct Devices {Device* at(IDirect3DDevice9*){return &ctx;}} devices;
enum class DrawMethod {Indexed};struct InputArgs{DrawMethod method;unsigned t,c,s;int b;unsigned m,n;};
int read_draw_input(Device&,IDirect3DDevice9*,InputArgs){return 0;}
void snapshot(IDirect3DDevice9*,const char*,unsigned,unsigned,bool,int,unsigned){++ctx.draws;}
void record_draw_input(Device&,int,HRESULT){}
template<class... A>void log(const char*,A...){}
#include "draw_under_test_inc.h"
int main(){unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u\n",unsigned(__LINE__));}}while(0)
    for(unsigned mode=0;mode<4;++mode){
        fp=incoming_fp;error=incoming_error;native_calls=scoped_calls=saved_getter_calls=0;events.clear();allow_submit=mode!=3;block=false;
        ctx.lattice_state=mode?std::make_shared<lattice_state::Capture>():nullptr;
        if(mode==2)ctx.lattice_state->selected=false;
        const HRESULT hr=draw_indexed(&device,4,-2,5,6,7,8);
        CHECK(hr==(allow_submit?native_result:-77));CHECK(native_calls==(allow_submit?1:0));
        CHECK(fp==(allow_submit?41:incoming_fp));CHECK(error==(allow_submit?43:incoming_error));
        if(allow_submit){CHECK(args_seen[0]==4&&args_seen[1]==unsigned(-2)&&args_seen[2]==5&&args_seen[3]==6&&args_seen[4]==7&&args_seen[5]==8);}
        if(mode==0){CHECK(scoped_calls==0);CHECK(saved_getter_calls==0);CHECK(events==std::vector<int>({2,4,5,6,8}));}
        if(mode==1){CHECK(scoped_calls==2);CHECK(saved_getter_calls==1);CHECK(events==std::vector<int>({1,2,3,4,5,6,7,8}));}
        if(mode==2){CHECK(scoped_calls==1);CHECK(saved_getter_calls==0);CHECK(events==std::vector<int>({1,2,4,5,6,8}));}
        if(mode==3){CHECK(saved_getter_calls==0);CHECK(events==std::vector<int>({1,2,4,6,7,8}));}
    }
    CHECK(logical_getter_calls==0);
    std::printf("lattice_state_hook checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
