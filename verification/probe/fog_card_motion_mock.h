// Host-only dependency double for the actual MotionOutput card methods. The
// test appends their unmodified production definitions at compile time.
#include "fog_card_policy.h"
#include "fog_sector_policy.h"
#include "fog_card_mask.h"
#include "fog_card_match.h"
#include "fog_pass_math.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <type_traits>
struct LARGE_INTEGER { long long QuadPart=0; };
inline void QueryPerformanceCounter(LARGE_INTEGER* v){v->QuadPart=1;}
inline void QueryPerformanceFrequency(LARGE_INTEGER* v){v->QuadPart=1;}
using HMODULE=void*;using LPCWSTR=const wchar_t*;
constexpr unsigned GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS=1,GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT=2;
inline bool GetModuleHandleExW(unsigned,LPCWSTR,HMODULE* m){*m=(void*)1;return true;}
constexpr int E_FAIL=-1,E_NOINTERFACE=-2,IID_IDirect3DTexture9=1;
using UINT=unsigned;constexpr int S_OK=0,S_FALSE=1,D3DERR_DEVICELOST=-200,D3DERR_DEVICENOTRESET=-201;
using DWORD=std::uint32_t; using HRESULT=std::int32_t;
constexpr bool FAILED(HRESULT hr) { return hr<0; }
constexpr bool SUCCEEDED(HRESULT hr) { return hr>=0; }
constexpr unsigned D3DPT_TRIANGLELIST=4,D3DDECLTYPE_FLOAT16_4=16,D3DZB_FALSE=0,D3DCULL_NONE=1,D3DFILL_SOLID=3,
 D3DBLEND_ONE=2,D3DBLEND_INVSRCCOLOR=4,D3DBLENDOP_ADD=1,SetRenderState=57,GetRenderState=58,GetStreamSourceFreq=103,GetRenderTarget=38;
constexpr unsigned D3DFMT_A32B32G32R32F=116;
using D3DRENDERSTATETYPE=unsigned;
constexpr unsigned motion_shadow_state_count=32,composition_blend_count=4;
constexpr unsigned shadow_states[32]={0,1,2,3,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,5,7};
constexpr unsigned composition_blend_states[4]={8,9,10,11};
enum { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_COLORWRITEENABLE,
 D3DRS_CULLMODE,D3DRS_STENCILENABLE,D3DRS_FILLMODE };
// Host stand-ins track temporary references; they do not render pixels.
struct IDirect3DTexture9 { unsigned refs=1;void Release(){assert(refs>1);--refs;} };
struct IDirect3DSurface9 {
 unsigned refs=1;IDirect3DTexture9 texture;
 HRESULT GetContainer(int,void** out){++texture.refs;*out=&texture;return S_OK;}
 void Release(){assert(refs>1);--refs;}
};
template<class T>void release(T*& value){if(value)value->Release();value=nullptr;}
struct Device {
 IDirect3DSurface9 target;
 static HRESULT get_target(Device* d,DWORD slot,IDirect3DSurface9** out){assert(slot==0);++d->target.refs;*out=&d->target;return S_OK;}
 long states[8]={0,0,0,1,7,1,0,3},blends[4]={2,4,1,0};
 unsigned gets=0,freq_gets=0,frequency=1;int failed_state=-1;bool fail_frequency=false;
 static HRESULT get(Device* d,unsigned state,DWORD* value){++d->gets;if(int(state)==d->failed_state)return -99;*value=DWORD(state<8?d->states[state]:d->blends[state-8]);return 0;}
 static HRESULT get_frequency(Device* d,unsigned stream,UINT* value){assert(stream==0);++d->freq_gets;if(d->fail_frequency)return -99;*value=d->frequency;return 0;}
 unsigned calls=0,draws=0; DWORD mask=7; unsigned fail_at=0; bool fail_restore=false,mutate=false;
 static HRESULT set(Device* d,unsigned state,DWORD mask) {
  assert(state==D3DRS_COLORWRITEENABLE); ++d->calls;
  if(d->calls==d->fail_at || (d->fail_restore&&d->calls==2)) { if(d->mutate){d->mask=mask;d->states[4]=mask;}return -99; }
  d->mask=mask;d->states[4]=mask;return 0;
 }
};
using GetRenderTargetFn=HRESULT(*)(Device*,DWORD,IDirect3DSurface9**);
using GetRenderStateFn=HRESULT(*)(Device*,unsigned,DWORD*);
using GetStreamFreqFn=HRESULT(*)(Device*,UINT,UINT*);
using SetRenderStateFn=HRESULT(*)(Device*,unsigned,DWORD);
template<class F> void call_preserved(F&& fn) { fn(); }
template<class... T> void log(const char*,T...) {}
namespace x3m::renderer {
enum class FogStage {None,Targets};
struct FogFrame {
 unsigned width=0,height=0;bool caller_scene_open=true;
 IDirect3DTexture9* depth_share=nullptr;IDirect3DSurface9* target=nullptr;
 fog_field::Profile profile=fog_field::Profile::None;unsigned recipe_id=0;std::uint64_t field_generation=0;
 bool main_target=false,linear_depth_current=false,caller_scene_known=false,caller_stateblock_recording=false,caller_queries_idle=true;
};
struct FogResult {
 bool applied=false;HRESULT restore=0;bool caller_state_restored=true,route_poisoned=false,scene_known=true,scene_open=true;
 HRESULT operation=0;FogStage failed=FogStage::None;unsigned cascades_bound=0,device_calls=0;
};
}
namespace x3m {
struct MotionDrawCall { bool indexed=true,user_memory=false;unsigned topology=4,primitives=2,vertex_count=4; };
struct MotionRoute { FogCardMask fog_card_mask{}; HRESULT preparation_error=0,submission_error=0;bool submit=true; };
enum class TaaInvalidateSite { FogTransition, StateLost };
enum class HdrState { Active, Off };
struct MockFog { struct Caps {bool enabled=true;}cap; const Caps& caps()const{return cap;}
 int prepare_field(void*,renderer::fog_field::Profile){return 0;}
 std::uint64_t field_generation()const{return 1;}
 int result=0;unsigned prepares=0;int prepare_targets(unsigned,unsigned){++prepares;if(result>=0)ready=true;return result;}
 unsigned executes=0;HRESULT execute_result=S_OK;bool execute_applied=true;
 HRESULT execute(const renderer::FogFrame& in,renderer::FogResult* out){
  assert(in.target&&in.depth_share&&in.main_target&&in.linear_depth_current&&in.caller_scene_known);
  ++executes;out->applied=execute_applied&&execute_result==S_OK;out->operation=execute_result;
  out->scene_open=in.caller_scene_open;return execute_result;
 }
 bool ready=true; bool resources_ready(unsigned w,unsigned h,renderer::fog_field::Profile p,unsigned,std::uint64_t generation)const{return ready&&w&&h&&p!=renderer::fog_field::Profile::None&&generation==1;} };
struct MockHdr { IDirect3DSurface9* surface=nullptr;IDirect3DSurface9* target()const{return surface;} };
struct MotionOutput {
 Device device_storage{}; Device* device_=&device_storage;
 struct Shadow { bool recording=false,fog_card_pair=true; std::uint64_t stream0=1,indices=2,declaration=0x0cdf6a8c884ad955ull;
  unsigned stream0_stride=24,position_offset=0,position_type=16;
  bool declaration_stream0_only=true;
  DWORD states[32]{},composition_blend[4]{};bool states_known[32]{},composition_blend_known[4]{},fill_mode_known=false;
 } shadow_;
 struct Counters { unsigned rs_queries=0,rs_hits=0,rs_gets=0;bool filled=true,cut=false; struct Taa {bool attempted=false;}taa;}counters_;
 renderer::FogSectorLatch fog_latch_{};
 FogCardPolicy fog_cards_{}; FogSectorFrame fog_sector_{};
 bool fog_everywhere_=false;std::uint64_t generation_=0,fog_transition_frame_=~std::uint64_t(0);
 struct SunFrame {bool failed=false,published=true;}sun_frame_;bool sun_lane_failed_=false;
 bool scene_open_=true,cut_finished_=false,fog_timing_=false;
 unsigned fog_failures_=0,fog_logs_=0,fog_card_logs_=0;
 std::uint64_t fog_applied_frames_=0,fog_card_last_report_=0,fog_card_observed_total_=0,
 fog_card_suppressed_total_=0,fog_card_refused_total_=0,fog_card_logged_frame_=0;
 const char* fog_last_reason_="";
 bool fog_requested_=true,fog_enabled_=true,fog_disabled_=false,fog_attach_failed_=false,fog_cards_replace_=true;
 float fog_strength_=.02f,fog_anisotropy_=.3f;
 bool fog_card_ready_checked_=false,fog_card_ready_=false,state_hooks_=false,composition_busy_=false,
 composition_state_lost_=false,motion_state_lost_=false,main_msaa_=false,taa_enabled_=true,taa_failed_=false,
 jitter_active_=true,depth_enabled_=true,bound=true,parameters_ready=true,prerequisites_ready=true;
 unsigned active_queries_=0,target_width_=64,target_height_=48,fog_card_mode_=0,invalidations=0,state_invalidations=0;
 std::uint64_t frame_=1,fog_frame_=0,id_=1;HRESULT motion_state_error_=0;
 const char* fog_card_fault_reason_="none";
 HdrState hdr_state_=HdrState::Active;
 MockHdr hdr_storage{};MockHdr* hdr_=&hdr_storage;
 MockFog fog_storage{};MockFog* fog_=&fog_storage;
 IDirect3DSurface9 depth_storage{};IDirect3DSurface9* depth_surface_=&depth_storage;
 struct Sampler {bool srgb_known=false,mipfilter_known=false,biased=false,saved_known=false;}samplers_[1];
 MotionOutput(){hdr_storage.surface=&device_storage.target;for(unsigned i=0;i<32;++i){shadow_.states[i]=DWORD(device_storage.states[shadow_states[i]]);shadow_.states_known[i]=true;}
  for(unsigned i=0;i<4;++i){shadow_.composition_blend[i]=DWORD(device_storage.blends[i]);shadow_.composition_blend_known[i]=true;}
  sample();fog_sector_.field_generation=1;}
 template<class F,class... A> HRESULT direct_call(unsigned slot,A...args){
  assert(slot==GetRenderState||slot==GetStreamSourceFreq);
  return slot==GetRenderState?Device::get(device_,args...):Device::get_frequency(device_,args...);
 }
 HRESULT get_render_state_native(D3DRENDERSTATETYPE,DWORD*)noexcept;
 bool state_known(unsigned)noexcept;
 bool blend_known(unsigned)noexcept;
 long state_field(unsigned)noexcept;
 void begin_draw_reads()noexcept;
 template<class F> F native(unsigned slot) {
  if constexpr(std::is_same_v<F,GetRenderTargetFn>){assert(slot==GetRenderTarget);return &Device::get_target;}
  else {assert(slot==SetRenderState);return &Device::set;}
 }
 void invalidate_taa(TaaInvalidateSite) {++invalidations;}
 void invalidate_render_states() {++state_invalidations;}
 unsigned depth_format_=D3DFMT_A32B32G32R32F;unsigned lane_depth_format()const{return depth_format_;}
 bool scene_bound()const{return bound;}
 long composition_blend_field(unsigned state)const{return shadow_.composition_blend_known[state]?long(shadow_.composition_blend[state]):-1;}
 const char* fog_frame_prerequisite()const{return prerequisites_ready?nullptr:"prerequisite";}
 const char* fog_frame_parameters(renderer::FogFrame&,float,bool&){return parameters_ready?nullptr:"parameters";}
 void fog_transition_invalidate()noexcept;
 void volumetric_fog_sector_sample(std::uint64_t,const sector_background::Sample&)noexcept;
 void fog_card_transition(unsigned)noexcept;
 void fault_fog_cards(const char*)noexcept;
 template<class F>void taa_call(F&& f){f();}
 bool attach_volumetric_fog(){return !fog_attach_failed_;}
 void prepare_volumetric_fog_targets(UINT,UINT)noexcept;
 void reconcile_volumetric_fog(const renderer::FogFrame&,const renderer::FogResult&,HRESULT)noexcept;
 void complete_volumetric_fog(const char*,HRESULT,const renderer::FogResult&)noexcept;
 void complete(bool success,const char*why){renderer::FogResult out{success,0};complete_volumetric_fog(success?nullptr:why,success?0:-1,out);}
 void run_volumetric_fog()noexcept;
 void disable_volumetric_fog(const char*,HRESULT)noexcept;
 void reset_fog_for_test()noexcept; // exact fog policy reset statements extracted by the host test
 void volumetric_fog_begin_frame()noexcept;
 void prepare_fog_card(const MotionDrawCall&,MotionRoute&)noexcept;
 void finish_fog_card(MotionRoute&,HRESULT)noexcept;
 int volumetric_fog_toggle()noexcept;
 int volumetric_fog_step()noexcept;
 void sample(const char* family="bluewell",unsigned sector=0x1000){sector_background::Sample s;s.status=sector_background::Status::Ready;
  s.row_valid=s.name_valid=true;s.dust=8;s.sector=sector;std::strcpy(s.family,family);volumetric_fog_sector_sample(frame_,s);}
 void next() {++frame_;volumetric_fog_begin_frame();sample();}

 void warm() {sample();volumetric_fog_begin_frame();complete(true,"ok");next();}
 HRESULT draw(HRESULT native_result=0,MotionDrawCall call={}) {
  if(!state_hooks_)begin_draw_reads();
  MotionRoute route;prepare_fog_card(call,route);
  const HRESULT result=route.submit?(++device_storage.draws,native_result):route.submission_error;
  if(route.fog_card_mask.masked)finish_fog_card(route,result);
  return result;
 }
};
} // namespace x3m
