// Host-only dependency double for the actual MotionOutput card methods. The
// test appends their unmodified production definitions at compile time.
#include "fog_card_policy.h"
#include "fog_card_mask.h"
#include "fog_card_match.h"
#include "fog_pass_math.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using UINT=unsigned;constexpr int S_OK=0,S_FALSE=1,D3DERR_DEVICELOST=-200,D3DERR_DEVICENOTRESET=-201;
using DWORD=std::uint32_t; using HRESULT=std::int32_t;
constexpr bool FAILED(HRESULT hr) { return hr<0; }
constexpr bool SUCCEEDED(HRESULT hr) { return hr>=0; }
constexpr unsigned D3DPT_TRIANGLELIST=4,D3DDECLTYPE_FLOAT16_4=16,D3DZB_FALSE=0,D3DCULL_NONE=1,D3DFILL_SOLID=3,
 D3DBLEND_ONE=2,D3DBLEND_INVSRCCOLOR=4,D3DBLENDOP_ADD=1,SetRenderState=57;
enum { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_COLORWRITEENABLE,
 D3DRS_CULLMODE,D3DRS_STENCILENABLE,D3DRS_FILLMODE };
struct Device {
 unsigned calls=0,draws=0; DWORD mask=7; unsigned fail_at=0; bool fail_restore=false,mutate=false;
 static HRESULT set(Device* d,unsigned state,DWORD mask) {
  assert(state==D3DRS_COLORWRITEENABLE); ++d->calls;
  if(d->calls==d->fail_at || (d->fail_restore&&d->calls==2)) { if(d->mutate)d->mask=mask;return -99; }
  d->mask=mask;return 0;
 }
};
using SetRenderStateFn=HRESULT(*)(Device*,unsigned,DWORD);
template<class F> void call_preserved(F&& fn) { fn(); }
template<class... T> void log(const char*,T...) {}
namespace x3m::renderer { struct FogFrame { unsigned width=0,height=0; }; struct FogResult {bool applied=false;HRESULT restore=0;}; }
namespace x3m {
struct MotionDrawCall { bool indexed=true,user_memory=false;unsigned topology=4,primitives=2,vertex_count=4; };
struct MotionRoute { FogCardMask fog_card_mask{}; HRESULT preparation_error=0,submission_error=0;bool submit=true; };
enum class TaaInvalidateSite { FogTransition };
enum class HdrState { Active, Off };
struct MockFog { struct Caps {bool enabled=true;}cap; const Caps& caps()const{return cap;}
 int result=0;unsigned prepares=0;int prepare(unsigned,unsigned){++prepares;if(result>=0)ready=true;return result;}
 bool ready=true; bool resources_ready(unsigned w,unsigned h)const{return ready&&w&&h;} };
struct MockHdr { void* target()const{return (void*)1;} };
struct MotionOutput {
 Device device_storage{}; Device* device_=&device_storage;
 struct Shadow { bool recording=false,fog_card_pair=true; std::uint64_t stream0=1,indices=2,declaration=0x0cdf6a8c884ad955ull;
  unsigned stream0_stride=24,position_offset=0,position_type=16,stream0_frequency=1;
  bool declaration_stream0_only=true,stream0_frequency_known=true;
 } shadow_;
 struct Counters { bool filled=true; struct Taa {bool attempted=false;}taa;}counters_;
 renderer::FogSectorLatch fog_latch_{};
 FogCardPolicy fog_cards_{};
 bool fog_requested_=true,fog_enabled_=true,fog_disabled_=false,fog_attach_failed_=false,fog_cards_replace_=true;
 float fog_strength_=.02f,fog_anisotropy_=.3f;
 bool fog_card_ready_checked_=false,fog_card_ready_=false,state_hooks_=true,composition_busy_=false,
 composition_state_lost_=false,motion_state_lost_=false,main_msaa_=false,taa_enabled_=true,taa_failed_=false,
 jitter_active_=true,depth_enabled_=true,bound=true,parameters_ready=true,prerequisites_ready=true;
 unsigned active_queries_=0,target_width_=64,target_height_=48,fog_card_mode_=0,invalidations=0,state_invalidations=0;
 std::uint64_t frame_=1,fog_frame_=0,id_=1;HRESULT motion_state_error_=0;
 const char* fog_card_fault_reason_="none";
 HdrState hdr_state_=HdrState::Active;
 MockHdr hdr_storage{};MockHdr* hdr_=&hdr_storage;
 MockFog fog_storage{};MockFog* fog_=&fog_storage;
 void* depth_surface_=(void*)1;
 long states[8]={0,0,0,1,7,1,0,3},blends[4]={2,4,1,0};
 template<class F> F native(unsigned slot) {assert(slot==SetRenderState);return &Device::set;}
 void invalidate_taa(TaaInvalidateSite) {++invalidations;}
 void invalidate_render_states() {++state_invalidations;}
 bool scene_bound()const{return bound;}
 long shadow_state_field(unsigned state)const{return states[state];}
 long composition_blend_field(unsigned state)const{return blends[state];}
 const char* fog_frame_prerequisite()const{return prerequisites_ready?nullptr:"prerequisite";}
 const char* fog_frame_parameters(renderer::FogFrame&,float,bool&){return parameters_ready?nullptr:"parameters";}
 void fog_card_transition(unsigned)noexcept;
 void fault_fog_cards(const char*)noexcept;
 template<class F>void taa_call(F&& f){f();}
 void prepare_volumetric_fog_targets(UINT,UINT)noexcept;
 void complete_volumetric_fog(const char*,HRESULT,const renderer::FogResult&)noexcept;
 void complete(bool success,const char*why){renderer::FogResult out{success,0};complete_volumetric_fog(success?nullptr:why,success?0:-1,out);}
 void volumetric_fog_begin_frame()noexcept;
 void prepare_fog_card(const MotionDrawCall&,MotionRoute&)noexcept;
 void finish_fog_card(MotionRoute&,HRESULT)noexcept;
 int volumetric_fog_toggle()noexcept;
 int volumetric_fog_step()noexcept;
 void next() {++frame_;volumetric_fog_begin_frame();}
 void warm() {volumetric_fog_begin_frame();complete(true,"ok");next();}
 HRESULT draw(HRESULT native_result=0,MotionDrawCall call={}) {
  MotionRoute route;prepare_fog_card(call,route);
  const HRESULT result=route.submit?(++device_storage.draws,native_result):route.submission_error;
  if(route.fog_card_mask.masked)finish_fog_card(route,result);
  return result;
 }
};
} // namespace x3m
