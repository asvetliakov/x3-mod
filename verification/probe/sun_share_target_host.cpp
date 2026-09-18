// Host API failure double around the ACTUAL extracted allocation/retirement
// methods. This owns CPU row-history/fallback proof; GPU contents remain a
// separate live/temporal fixture requirement.
#include "src/renderer/motion_row_history.h"
#include "src/renderer/sun_share_frame.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
using UINT=unsigned;using DWORD=unsigned;using HRESULT=int;using D=void*;
enum D3DFORMAT {D3DFMT_R32F=114,D3DFMT_G32R32F=115,D3DFMT_A32B32G32R32F=116};
constexpr int S_OK=0,S_FALSE=1,E_FAIL=-1,D3DUSAGE_RENDERTARGET=1,D3DPOOL_DEFAULT=0,CreateTexture=23;
bool SUCCEEDED(HRESULT h){return h>=0;}bool FAILED(HRESULT h){return h<0;}
static int fail_format=0;static bool fail_level=false;static unsigned live=0,creates=0;
struct IDirect3DSurface9 {D3DFORMAT format;explicit IDirect3DSurface9(D3DFORMAT f):format(f){++live;}~IDirect3DSurface9(){--live;}};
struct IDirect3DTexture9 {D3DFORMAT format;HRESULT GetSurfaceLevel(unsigned,IDirect3DSurface9** out){if(fail_level)return E_FAIL;*out=new IDirect3DSurface9(format);return S_OK;}};
template<class T>void release(T*& p){delete p;p=nullptr;}
using CreateTextureFn=HRESULT(*)(D,UINT,UINT,UINT,DWORD,D3DFORMAT,DWORD,IDirect3DTexture9**,void*);
HRESULT create(D,UINT,UINT,UINT,DWORD,D3DFORMAT format,DWORD,IDirect3DTexture9** out,void*){
    ++creates;if(int(format)==fail_format)return E_FAIL;*out=new IDirect3DTexture9{format};return S_OK;
}
struct MotionOutput {
    D device_=nullptr;bool sun_lane_requested_=true,sun_lane_qualified_=true,sun_lane_active_=false,sun_lane_failed_=false,target_failed_=false,depth_enabled_=true;
    bool sun_lane_linear_depth_=false; // the receiver-depth option (motion_output.h lane_depth_format)
    D3DFORMAT lane_depth_format() const noexcept { return sun_lane_active_?(sun_lane_linear_depth_?D3DFMT_A32B32G32R32F:D3DFMT_G32R32F):D3DFMT_R32F; }
    struct {unsigned format=77;} main_depth_;
    std::uint64_t id_=1,generation_=1,target_generation_=0;
    UINT target_width_=0,target_height_=0;
    IDirect3DSurface9 *target_surface_=nullptr,*depth_surface_=nullptr;
    x3m::renderer::SunShareFrame sun_frame_;
    x3m::renderer::MotionRowHistory history_{16};
    bool sun_lane_depth_qualified(D3DFORMAT)const{return true;}
    template<class F>F native(unsigned){return static_cast<F>(create);}
    template<class... T>void log(const char*,T...){}
    bool ensure_target(UINT,UINT)noexcept;void release_target()noexcept;
    bool latch(UINT w,UINT h){const bool ok=ensure_target(w,h);if(ok)history_.begin_frame({generation_,w,h});else history_.invalidate();return ok;}
    ~MotionOutput(){release_target();}
};
/* MOTION_OUTPUT_METHODS */
using namespace x3m::renderer;
static unsigned checks=0;
void check(bool value){++checks;if(!value)throw std::runtime_error("sun target host check "+std::to_string(checks));}
RigidDrawKey key(unsigned node){RigidDrawKey k{};k.object_lifetime=node;k.camera_lifetime=k.draw_domain=k.camera=k.vertex_buffer=k.declaration=k.position_program=k.stride=k.primitives=k.pass=1;k.node=node;return k;}
SubmittedMatrix matrix(){SubmittedMatrix m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
void seed(MotionOutput& output){SubmittedMatrix previous{};check(output.latch(64,64));check(!output.history_.lookup_and_record(key(1),matrix(),previous));check(!output.history_.lookup_and_record(key(2),matrix(),previous));check(output.history_.commit(true));}
int main(){
    for(unsigned change=0;change<7;++change){
        MotionOutput output;seed(output);output.sun_lane_failed_=true;
        unsigned width=64,height=64;
        if(change==1)width=32;                 // Resize invalidates.
        if(change==2)++output.generation_;    // Reset generation invalidates.
        if(change==3)output.release_target(); // Loss/retirement invalidates.
        if(change==4){output.sun_lane_active_=false;width=32;} // Ordinary resize invalidates.
        if(change==5)fail_format=D3DFMT_R32F;  // Failed ordinary depth allocation.
        if(change==6)fail_level=true;         // Failed motion surface extraction.
        const bool ok=output.latch(width,height);check(ok==(change<5));
        SubmittedMatrix previous{};
        check(output.history_.lookup_and_record(key(1),matrix(),previous)==(change==0));
        if(change==0){
            check(previous==matrix());check(output.history_.lookup_and_record(key(2),matrix(),previous));
            check(!output.history_.lookup_and_record(key(1),matrix(),previous)); // Original fixture's duplicate fault.
            check(output.depth_surface_->format==D3DFMT_R32F&&!output.sun_lane_active_);
        }
        if(change>=5)check(!output.target_surface_&&!output.depth_surface_&&output.target_failed_);
        fail_format=0;fail_level=false;
    }
    check(live==0);
    std::printf("SUN_TARGET_HOST_PASS checks=%u creates=%u live=%u\n",checks,creates,live);
}
