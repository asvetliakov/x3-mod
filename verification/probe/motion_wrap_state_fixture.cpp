// Host execution of unchanged production WRAP transaction methods. Scripted
// COM failures include mutation-before-failure; no D3D/Wine implementation.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>
using DWORD=std::uint32_t; using HRESULT=std::int32_t; using UINT=unsigned;
constexpr HRESULT S_OK=0,E_FAIL=-1,D3DERR_INVALIDCALL=-2,E_FIRST=-11,E_LATER=-12;
constexpr bool SUCCEEDED(HRESULT h){return h>=0;} constexpr bool FAILED(HRESULT h){return h<0;}
using D3DRENDERSTATETYPE=unsigned;
constexpr unsigned D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_ALPHABLENDENABLE=27,
 D3DRS_COLORWRITEENABLE=168,D3DRS_SRGBWRITEENABLE=194,D3DRS_COLORWRITEENABLE1=190,D3DRS_COLORWRITEENABLE2=191,
 D3DRS_WRAP0=128,D3DRS_WRAP1=129,D3DRS_WRAP2=130,D3DRS_WRAP3=131,D3DRS_WRAP4=132,D3DRS_WRAP5=133,D3DRS_WRAP6=134,D3DRS_WRAP7=135,
 D3DRS_WRAP8=198,D3DRS_WRAP9=199,D3DRS_WRAP10=200,D3DRS_WRAP11=201,D3DRS_WRAP12=202,D3DRS_WRAP13=203,D3DRS_WRAP14=204,D3DRS_WRAP15=205;
constexpr unsigned D3DRS_ALPHAFUNC=25,D3DRS_ALPHAREF=24,D3DRS_ZFUNC=23,D3DRS_FOGENABLE=28,D3DRS_DITHERENABLE=26,
 D3DRS_STENCILENABLE=52,D3DRS_CULLMODE=22,D3DRS_FILLMODE=8,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_BLENDOP=171,D3DRS_SEPARATEALPHABLENDENABLE=206,D3DRS_SRCBLENDALPHA=207,D3DRS_DESTBLENDALPHA=208,D3DRS_BLENDOPALPHA=209,D3DRS_BLENDFACTOR=193;
constexpr unsigned motion_shadow_state_count=32,failure_log_limit=16;
namespace renderer {
struct MotionOutputProfile{std::uint8_t texcoord_index=4,depth_texcoord_index=7;};
struct LinearMaterialScalarTransport{
 std::uint8_t source_texcoord=0,source_component=0,destination_texcoord=0,destination_component=0;
};
struct LinearMaterialPairContract{
 std::uint32_t sampler_mask=0;bool bump=false;
 std::array<LinearMaterialScalarTransport,2> scalar_transport{};std::uint8_t scalar_transport_count=0;
};
}
struct MotionRoute {
 HRESULT preparation_error=S_OK;
 bool depth=false,linear_material=false,write2_set=false,rt2_set=false,write_set=false,rt_set=false,ps_set=false,vs_set=false,vs_constants_set=false,ps_constants_set=false;
 DWORD saved_write1=15,saved_write2=15,saved_wrap[6]{};std::uint8_t wrap_index[6]{},wrap_count=0,wrap_attempted=0;
 bool fade_arm=false,sun_receiver=false; // fade-band arm and sun-share lane flags read by the bind path
 bool original_fill=false; // X3M_ORIGINAL_FILL: the bind path records the fill variant it selected
 bool hull_lightmap=false; // hull light-map gain PS selected in the routed pair
};
static unsigned failures=0,checks=0,allocations=0;
void* operator new(std::size_t n){++allocations;if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u: %s\n",__LINE__,#x);}}while(0)
struct Device {
 std::array<DWORD,256> states{};unsigned gets=0,sets=0,vs_sets=0,ps_sets=0;
 std::array<unsigned,16> fail_get{},fail_set{};
 std::array<HRESULT,64> set_result{};
 bool mutation_before_failure=false;void*fail_shader=nullptr,*bound_vs=nullptr,*bound_ps=nullptr;
 std::array<std::pair<unsigned,DWORD>,64> writes{};
};
using GetRenderStateFn=HRESULT(*)(Device*,D3DRENDERSTATETYPE,DWORD*);
using SetRenderStateFn=HRESULT(*)(Device*,D3DRENDERSTATETYPE,DWORD);
using SetPsFn=HRESULT(*)(Device*,void*);using SetVsFn=SetPsFn;
using SetConstantsFFn=HRESULT(*)(Device*,unsigned,const float*,unsigned);
enum{GetRenderState,SetRenderState,SetPixelShader,SetVertexShader,SetVertexShaderConstantF,SetPixelShaderConstantF};
HRESULT get_state(Device*d,unsigned s,DWORD*v){++d->gets;for(auto n:d->fail_get)if(n==d->gets)return E_FAIL;*v=d->states[s];return S_OK;}
HRESULT set_state(Device*d,unsigned s,DWORD v){
 ++d->sets;d->writes[(d->sets-1)%d->writes.size()]={s,v};HRESULT result=d->set_result[d->sets];
 if(SUCCEEDED(result))for(auto n:d->fail_set)if(n==d->sets){result=E_FAIL;break;}
 if(SUCCEEDED(result)||d->mutation_before_failure)d->states[s]=v;return result;
}
HRESULT set_vs(Device*d,void*p){++d->vs_sets;if(p==d->fail_shader)return E_FAIL;d->bound_vs=p;return S_OK;}
HRESULT set_ps(Device*d,void*p){++d->ps_sets;if(p==d->fail_shader)return E_FAIL;d->bound_ps=p;return S_OK;}
HRESULT set_constants(Device*,unsigned,const float*,unsigned){return S_OK;}
template<class...A>void log(const char*,A...){ }
struct Pass {void after_reset(HRESULT){};};
// Mirrors src/proxy/motion_output.h's TaaInvalidateSite; this double only
// counts the calls, so the names exist for the extracted code to compile.
enum class TaaInvalidateSite:unsigned{RestoreFailed=0,StateLost=1,Skip=2,Target=3,Container=4,ResolveFailed=5,NotResolved=6,PresentFailed=7,Reset=8,ComparisonExposure=9,ComparisonStateFailed=10,CompositionStateLost=11,CompositionReaders=12,CompositionExport=13,CompositionAttach=14,CompositionBegin=15,CompositionRefused=16,CompositionPrepare=17,CompositionIncomplete=18,CutoutMissed=19,Count=20};
class MotionOutput {
public:
 Device*device_;bool enabled_=true,state_shadow_=true,state_hooks_=true,motion_state_lost_=false,scene_open_=false; // state_hooks_: hybrid unhook mirror (hooks on here)
 HRESULT motion_state_error_=D3DERR_INVALIDCALL;
 unsigned id_=1,generation_=0,frame_=0,taa_references_=0,logged_failures_=0,taa_invalidations=0,frames=0;
 Pass*taa_=nullptr;
 Pass*ao_=nullptr; // AO step 2 (8b0a7c1): after_reset forwards to the ambient-occlusion pass when one is attached
 Pass*depth_replay_=nullptr; // cascade-0 depth replay: after_reset forwards to the pass when one is attached
 Pass*sun_apply_=nullptr; // scene-end sun-shadow apply: after_reset forwards to the pass when one is attached
 std::uint64_t sun_apply_frame_=~std::uint64_t(0),depth_replayed_frame_=~std::uint64_t(0); // per-frame markers cleared by after_reset
 struct{unsigned rs_queries=0,rs_hits=0,rs_gets=0,rs_resyncs=0,restore_failures=0,draws=0,sb_resyncs=0,material_bind_failures=0;}counters_;
 struct{DWORD states[motion_shadow_state_count]{};bool states_known[motion_shadow_state_count]{};bool recording=false;
  /* sized for the production composition_blend_states table (asserted below) */ DWORD composition_blend[8]{};bool composition_blend_known[8]{};DWORD fill_mode=0;bool fill_mode_known=false;
  bool vs_reserved_written=false,ps_reserved_written=false;void*vs=nullptr,*ps=nullptr,*vs_variant=nullptr,*ps_variant=nullptr,*vs_material_variant=nullptr,*ps_material_variant=nullptr;
  bool original_fill_pair=false;void*ps_original_fill_variant=nullptr;
    bool xt_default_pair=false,xt_default_ready=false;void*vs_xt_default_linear=nullptr,*vs_xt_default_ordinary=nullptr,*ps_xt_default_ordinary=nullptr;
  void*ps_sun_motion=nullptr,*ps_sun_material=nullptr,*ps_sun_xt=nullptr;bool ps_sun_extraction=false;
  void*ps_sun_original=nullptr;bool original_share_pair=false,original_share_refused=false; // original share variant (legacy-sun-application.md 4.1)
  void*ps_sun_original_lightmap=nullptr,*ps_hull_lightmap_variant=nullptr;bool hull_lightmap_pair=false; // hull light-map gain variants; inert here
  float vs_reserved[16]{},ps_reserved[8]{};renderer::LinearMaterialPairContract material_contract{};
 }shadow_;
 explicit MotionOutput(Device&d):device_(&d){}
 template<class F,class...A> HRESULT direct_call(unsigned n,A...a){return native<F>(n)(device_,a...);} // the route's value-only entry
 template<class F> F native(unsigned n){switch(n){case GetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_state));case SetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(set_state));
 case SetPixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_ps));case SetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_vs));default:return reinterpret_cast<F>(reinterpret_cast<void*>(set_constants));}}
 HRESULT bind_target(unsigned,void*){return S_OK;}
 // Mirrors motion_output.h; the bind path reads it for the original-fill gate.
 enum class HdrState{Off,Active,Suspended};HdrState hdr_state_=HdrState::Active;
 bool cutout_reset_pending_=false; void probe_cutout_caps(bool){}
 bool composition_requested()const{return false;}
 bool blend_shadow_requested()const{return false;}
 // Sun-share lane (X3M_SUN_SHADOW_LANE): inert for the wrap-state seam; the
 // extracted bind path only reads the flags and reports a failed creation.
 bool sun_lane_requested_=false,sun_lane_qualified_=false,sun_lane_active_=false,sun_lane_failed_=false;
 bool original_fill_requested_=false; unsigned sun_original_refused_draws_=0; // read by the bind path's original-share gate
 bool hull_gain_enabled_=true,hull_lightmap_enabled_=true; std::uint32_t hull_lightmap_draws_=0; // F6 guide-light flag, F4 light-map flag and light-map draw counter read by the bind/after-draw paths
 unsigned sun_qualifications_=0;void qualify_sun_lane(){++sun_qualifications_;}
 struct{bool failed=false,published=false,available=false,coverage_required=false;unsigned receivers=0,covered=0,untracked=0;}sun_frame_;
 bool screen_emission_bound_=false; // step B locked-prefix request; inert for the wrap-state seam
 void invalidate_taa(TaaInvalidateSite){++taa_invalidations;}
 void resync_shadow(){invalidate_render_states();}
 void begin_frame(unsigned,bool){++frames;}
 void set_render_state(D3DRENDERSTATETYPE,DWORD) noexcept;
 HRESULT get_render_state_native(D3DRENDERSTATETYPE,DWORD*) noexcept;
 HRESULT render_state(D3DRENDERSTATETYPE,DWORD*) noexcept;
 void invalidate_render_states() noexcept;
 HRESULT apply_wrap_states(MotionRoute&,const renderer::MotionOutputProfile&) noexcept;
 HRESULT restore_wrap_states(MotionRoute&) noexcept;
 void recover_motion_state() noexcept;
 HRESULT undo(MotionRoute&) noexcept;
 HRESULT bind_variant_pair(MotionRoute&,bool) noexcept;
 void after_reset(HRESULT) noexcept;
 void begin_stateblock() noexcept;void end_stateblock() noexcept;void stateblock_applied() noexcept;
};

// Inert mirror of the X3M_FRAME_TIMING redundant-state counters
// (src/proxy/frame_timing.h): the extracted shadow updates report into them
// and they measure nothing here.
namespace frame_timing {
enum class StateSet : unsigned { RenderState = 0, SamplerState = 1, Texture = 2 };
inline void state_write(StateSet, unsigned, bool, bool) noexcept {}
}
#include "motion_wrap_under_test_inc.h"
static_assert(composition_blend_count<=8,"blend shadow mirror is smaller than the production table");

unsigned wrap(unsigned i){return i<8?D3DRS_WRAP0+i:D3DRS_WRAP8+i-8;}
renderer::LinearMaterialPairContract material(std::uint8_t count,
 renderer::LinearMaterialScalarTransport a={},renderer::LinearMaterialScalarTransport b={}){
 renderer::LinearMaterialPairContract result{};result.sampler_mask=31;result.bump=true;
 result.scalar_transport={a,b};result.scalar_transport_count=count;return result;
}
void app_state(Device&d,MotionOutput&m,unsigned index,DWORD value){d.states[wrap(index)]=value;m.set_render_state(wrap(index),value);}

// Preserve the previous exhaustive temporal/depth contract unchanged. A
// non-material fallback must never consume cached material relocations.
void roundtrips(){
 for(bool cached:{false,true})for(bool depth:{false,true})for(unsigned a=0;a<16;++a)for(unsigned b=0;b<16;++b){
  if(a==b)continue;Device d;MotionOutput m(d);m.state_shadow_=cached;
  for(unsigned i=0;i<16;++i){app_state(d,m,i,i+1);CHECK(shadow_index(wrap(i))==i+8);}
  m.shadow_.material_contract=material(2,{6,0,1,3},{6,1,2,3});
  const auto before=d.states;MotionRoute r;r.depth=depth;renderer::MotionOutputProfile p{std::uint8_t(a),std::uint8_t(b)};
  CHECK(m.apply_wrap_states(r,p)==S_OK);for(unsigned i=0;i<16;++i)CHECK(d.states[wrap(i)]==((i==a||(depth&&i==b))?0:i+1));
  CHECK(m.undo(r)==S_OK&&d.states==before&&!m.motion_state_lost_);CHECK(d.sets==(depth?4u:2u));CHECK(d.gets==(cached?0u:depth?2u:1u));
 }
}

void actual_material_contracts(){
 const renderer::MotionOutputProfile boron_row{7,8},paranid_row{5,8};
 const auto boron_base=material(2,{6,0,1,3},{6,1,2,3});
 const auto boron_single=material(1,{6,0,1,3});
 const auto paranid=material(1,{7,0,1,3});
 // Real Boron BUMP row: motion TEX7, depth TEX8, with native TEX6.x/y
 // transported to TEX1.w/TEX2.w. Exercise the compiled depth-off/on variants.
 for(bool depth:{false,true})for(DWORD source=0;source<4;++source){
  Device d;MotionOutput m(d);m.shadow_.material_contract=boron_base;
  app_state(d,m,7,15);app_state(d,m,8,10);app_state(d,m,6,source);
  app_state(d,m,1,(source&1)?0:15);app_state(d,m,2,(source&2)?0:15);
  const auto before=d.states;MotionRoute r;r.depth=depth;r.linear_material=true;
  CHECK(m.apply_wrap_states(r,boron_row)==S_OK&&r.wrap_count==(depth?5:4));
  CHECK(d.states[wrap(7)]==0&&d.states[wrap(8)]==(depth?0u:10u)&&d.states[wrap(6)]==source);
  CHECK(d.states[wrap(1)]==((before[wrap(1)]&~8u)|((source&1)?8u:0u)));
  CHECK(d.states[wrap(2)]==((before[wrap(2)]&~8u)|((source&2)?8u:0u)));
  CHECK(m.undo(r)==S_OK&&d.states==before);
 }
 // Boron single retains that TEX7/TEX8 row. Paranid BUMP uses motion TEX5,
 // depth TEX8 and preserves its native TEX7 source.
 auto single=[&](const renderer::LinearMaterialPairContract& contract,
                  const renderer::MotionOutputProfile& row,unsigned source_index){
  for(bool depth:{false,true})for(DWORD source:{0u,1u}){Device d;MotionOutput m(d);m.shadow_.material_contract=contract;
   app_state(d,m,row.texcoord_index,9);app_state(d,m,row.depth_texcoord_index,10);
   app_state(d,m,source_index,source);app_state(d,m,1,6|(source?0u:8u));
   const auto before=d.states;MotionRoute r;r.depth=depth;r.linear_material=true;
   CHECK(m.apply_wrap_states(r,row)==S_OK&&r.wrap_count==(depth?4:3));
   CHECK(d.states[wrap(row.texcoord_index)]==0&&
         d.states[wrap(row.depth_texcoord_index)]==(depth?0u:10u)&&d.states[wrap(source_index)]==source);
   CHECK(d.states[wrap(1)]==((before[wrap(1)]&~8u)|(source?8u:0u)));
   CHECK(m.undo(r)==S_OK&&d.states==before);
  }
 };
 single(boron_single,boron_row,6);single(paranid,paranid_row,7);
}

void fallback_and_alias_cases(){
 const renderer::MotionOutputProfile p{4,5};const auto boron=material(2,{6,0,1,3},{6,1,2,3});
 // Failed combined VS/PS binding retries the actual ordinary variants. The
 // real bind method clears linear_material, so only temporal WRAP is changed.
 for(bool fail_vs:{true,false}){Device d;MotionOutput m(d);int vs=1,ps=2,mvs=3,mps=4;
  m.shadow_.vs=&vs;m.shadow_.ps=&ps;m.shadow_.vs_variant=&vs;m.shadow_.ps_variant=&ps;
  m.shadow_.vs_material_variant=&mvs;m.shadow_.ps_material_variant=&mps;m.shadow_.material_contract=boron;
  d.fail_shader=fail_vs?static_cast<void*>(&mvs):static_cast<void*>(&mps);
  app_state(d,m,4,15);app_state(d,m,1,7);app_state(d,m,2,8);app_state(d,m,6,3);const auto before=d.states;
  MotionRoute r;CHECK(m.bind_variant_pair(r,true)==S_OK&&!r.linear_material&&m.counters_.material_bind_failures==1);
  CHECK(d.bound_vs==&vs&&d.bound_ps==&ps);CHECK(m.apply_wrap_states(r,p)==S_OK&&r.wrap_count==1&&d.states[wrap(4)]==0);
  CHECK(d.states[wrap(1)]==before[wrap(1)]&&d.states[wrap(2)]==before[wrap(2)]&&d.states[wrap(6)]==before[wrap(6)]);
  CHECK(m.undo(r)==S_OK&&d.states==before);
 }
 // A source may alias another mapping's destination. Both mappings use the
 // original snapshot, rather than the first mapping's new value.
 {Device d;MotionOutput m(d);m.shadow_.material_contract=material(2,{6,0,1,3},{1,3,2,3});
  app_state(d,m,4,1);app_state(d,m,6,0);app_state(d,m,1,8|5);app_state(d,m,2,2);
  const auto before=d.states;MotionRoute r;r.linear_material=true;CHECK(m.apply_wrap_states(r,p)==S_OK);
  CHECK(d.states[wrap(1)]==5&&d.states[wrap(2)]==10);CHECK(m.undo(r)==S_OK&&d.states==before);
 }
 // Distinct destination components share one snapshot and one setter.
 {Device d;MotionOutput m(d);m.shadow_.material_contract=material(2,{6,0,1,3},{7,1,1,2});
  app_state(d,m,4,0);app_state(d,m,6,1);app_state(d,m,7,0);app_state(d,m,1,15);
  const auto before=d.states;MotionRoute r;r.linear_material=true;CHECK(m.apply_wrap_states(r,p)==S_OK);
  CHECK(r.wrap_count==4&&d.gets==0&&d.sets==1&&d.states[wrap(1)]==11);
  CHECK(m.undo(r)==S_OK&&d.sets==2&&d.states==before);
 }
 // The two real Boron records alias TEX6 and read it once with shadow disabled.
 {Device d;MotionOutput m(d);m.state_shadow_=false;m.shadow_.material_contract=boron;
  d.states[wrap(4)]=d.states[wrap(5)]=0;d.states[wrap(6)]=3;d.states[wrap(1)]=d.states[wrap(2)]=0;
  MotionRoute r;r.depth=true;r.linear_material=true;CHECK(m.apply_wrap_states(r,p)==S_OK&&r.wrap_count==5&&d.gets==5);CHECK(m.undo(r)==S_OK);
 }
}

void failure_cases(){
 const renderer::MotionOutputProfile basic{0,15},full{4,5};
 for(unsigned zero:{0u,15u}){Device d;d.states[wrap(0)]=5;d.states[wrap(15)]=9;d.states[wrap(zero)]=0;auto before=d.states;MotionOutput m(d);MotionRoute r;r.depth=true;
  CHECK(m.apply_wrap_states(r,basic)==S_OK&&d.sets==1);CHECK(m.undo(r)==S_OK&&d.sets==2&&d.states==before);}
 for(unsigned bad:{0u,1u}){Device d;MotionOutput m(d);MotionRoute r;r.depth=true;auto invalid=basic;if(bad)invalid.depth_texcoord_index=255;else invalid.texcoord_index=16;
  CHECK(m.apply_wrap_states(r,invalid)==D3DERR_INVALIDCALL&&m.undo(r)==S_OK&&d.sets==0);}
 const auto distinct=material(2,{6,0,1,3},{7,1,2,2});
 // Every unique source/destination/temporal read occurs before any setter.
 for(unsigned n=1;n<=6;++n){Device d;MotionOutput m(d);m.state_shadow_=false;m.shadow_.material_contract=distinct;
  for(unsigned i:{1u,2u,4u,5u,6u,7u})d.states[wrap(i)]=15;const auto before=d.states;d.fail_get[0]=n;
  MotionRoute r;r.depth=true;r.linear_material=true;CHECK(FAILED(m.apply_wrap_states(r,full))&&d.gets==n&&d.sets==0);
  CHECK(m.undo(r)==S_OK&&d.states==before&&!m.motion_state_lost_);}
 // Every possible setter failure is restored, including mutation before failure.
 for(bool mutation:{false,true})for(unsigned n=1;n<=4;++n){Device d;MotionOutput m(d);m.shadow_.material_contract=distinct;
  app_state(d,m,4,15);app_state(d,m,5,15);app_state(d,m,6,1);app_state(d,m,7,0);app_state(d,m,1,0);app_state(d,m,2,15);
  const auto before=d.states;d.fail_set[0]=n;d.mutation_before_failure=mutation;MotionRoute r;r.depth=true;r.linear_material=true;
  CHECK(FAILED(m.apply_wrap_states(r,full)));CHECK(m.undo(r)==S_OK&&d.states==before&&!m.motion_state_lost_);CHECK(d.sets==n*2);}
 // Failed restores continue in exact reverse order and quarantine the route.
 for(bool mutation:{false,true})for(unsigned n=1;n<=4;++n){Device d;MotionOutput m(d);m.shadow_.material_contract=distinct;
  app_state(d,m,4,15);app_state(d,m,5,15);app_state(d,m,6,1);app_state(d,m,7,0);app_state(d,m,1,0);app_state(d,m,2,15);
  d.fail_set[0]=4+n;d.mutation_before_failure=mutation;MotionRoute r;r.depth=true;r.linear_material=true;
  CHECK(m.apply_wrap_states(r,full)==S_OK);CHECK(FAILED(m.undo(r))&&m.motion_state_lost_&&m.motion_state_error_==E_FAIL);
  CHECK(d.sets==8&&m.counters_.restore_failures==1&&m.taa_invalidations==1);
  CHECK(d.writes[4].first==wrap(2)&&d.writes[5].first==wrap(1)&&d.writes[6].first==wrap(5)&&d.writes[7].first==wrap(4));
  CHECK(m.undo(r)==S_OK&&d.sets==8);
 }
 // The first restoration HRESULT survives later cleanup failures.
 {Device d;MotionOutput m(d);m.shadow_.material_contract=distinct;
  app_state(d,m,4,15);app_state(d,m,5,15);app_state(d,m,6,1);app_state(d,m,7,0);app_state(d,m,1,0);app_state(d,m,2,15);
  d.set_result[5]=E_FIRST;d.set_result[7]=E_LATER;MotionRoute r;r.depth=true;r.linear_material=true;
  CHECK(m.apply_wrap_states(r,full)==S_OK);CHECK(m.undo(r)==E_FIRST&&m.motion_state_error_==E_FIRST&&m.motion_state_lost_);CHECK(d.sets==8);
 }
}

void malformed_contracts(){
 auto refuse=[&](renderer::LinearMaterialPairContract contract,
                 renderer::MotionOutputProfile row=renderer::MotionOutputProfile{4,5},bool depth=true){
  Device d;MotionOutput m(d);m.shadow_.material_contract=contract;for(unsigned i=0;i<16;++i)d.states[wrap(i)]=i;
  const auto before=d.states;MotionRoute r;r.depth=depth;r.linear_material=true;
  CHECK(m.apply_wrap_states(r,row)==D3DERR_INVALIDCALL&&d.gets==0&&d.sets==0&&d.states==before);CHECK(m.undo(r)==S_OK);
 };
 auto valid=material(1,{6,0,1,3});
 {auto c=valid;c.scalar_transport_count=3;refuse(c);}
 {auto c=valid;c.sampler_mask=0;refuse(c);}
 {auto c=valid;c.scalar_transport[0].source_texcoord=16;refuse(c);}
 {auto c=valid;c.scalar_transport[0].destination_texcoord=16;refuse(c);}
 {auto c=valid;c.scalar_transport[0].source_component=4;refuse(c);}
 {auto c=valid;c.scalar_transport[0].destination_component=4;refuse(c);}
 {auto c=valid;c.scalar_transport[0].source_texcoord=4;refuse(c);}
 {auto c=valid;c.scalar_transport[0].destination_texcoord=4;refuse(c);}
 {auto c=valid;c.scalar_transport[0].source_texcoord=5;refuse(c);}
 {auto c=valid;c.scalar_transport[0].destination_texcoord=5;refuse(c);}
 {auto c=material(2,{6,0,1,3},{7,1,1,3});refuse(c);}
 refuse(valid,{4,4});
}

void shadow_reset_and_performance(){
 Device d;MotionOutput m(d);const renderer::MotionOutputProfile p{4,5};
 m.shadow_.material_contract=material(2,{6,0,1,3},{6,1,2,3});MotionRoute r;r.depth=true;r.linear_material=true;
 CHECK(m.apply_wrap_states(r,p)==S_OK&&m.undo(r)==S_OK&&d.gets==5&&d.sets==0);
 auto gets=d.gets;CHECK(m.apply_wrap_states(r,p)==S_OK&&m.undo(r)==S_OK&&d.gets==gets&&d.sets==0);
 // Recorded writes preserve the logical cache. End/Apply invalidate every slot.
 set_state(&d,wrap(6),3);m.set_render_state(wrap(6),3);m.begin_stateblock();m.set_render_state(wrap(6),0);CHECK(m.shadow_.states[14]==3);m.end_stateblock();
 d.states[wrap(1)]=0;d.states[wrap(2)]=15;CHECK(m.apply_wrap_states(r,p)==S_OK&&d.states[wrap(1)]==8&&d.states[wrap(2)]==15);CHECK(m.undo(r)==S_OK);
 d.states[wrap(6)]=0;d.states[wrap(1)]=d.states[wrap(2)]=15;m.stateblock_applied();
 CHECK(m.apply_wrap_states(r,p)==S_OK&&d.states[wrap(1)]==7&&d.states[wrap(2)]==7);CHECK(m.undo(r)==S_OK);
 m.state_shadow_=false;d.states[wrap(6)]=3;d.states[wrap(1)]=d.states[wrap(2)]=0;
 CHECK(m.apply_wrap_states(r,p)==S_OK&&d.states[wrap(1)]==8&&d.states[wrap(2)]==8);CHECK(m.undo(r)==S_OK);
 // Reset quarantine clears only after all sixteen native states resynchronize.
 m.state_shadow_=true;m.motion_state_lost_=true;m.motion_state_error_=E_FAIL;m.after_reset(E_FAIL);CHECK(m.motion_state_lost_);
 d.fail_get[0]=d.gets+4;m.after_reset(S_OK);CHECK(m.motion_state_lost_);d.fail_get={};d.states={};m.after_reset(S_OK);
 CHECK(!m.motion_state_lost_&&m.motion_state_error_==D3DERR_INVALIDCALL);
 // Cached steady state does no lookup, allocation, native getter or setter.
 m.shadow_.material_contract=material(2,{6,0,1,3},{6,1,2,3});r.depth=true;r.linear_material=true;
 CHECK(m.apply_wrap_states(r,p)==S_OK&&m.undo(r)==S_OK);
 const auto alloc=allocations,reads=d.gets,writes=d.sets;const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<200000;++i){MotionRoute t;t.depth=true;t.linear_material=true;m.apply_wrap_states(t,p);m.undo(t);}
 const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
 CHECK(allocations==alloc&&d.gets==reads&&d.sets==writes);
 std::printf("palette_wrap_host_zero iterations=200000 ns_per_transaction=%.2f native_gets=0 native_sets=0 allocations=0 lookups=0\n",double(ns)/200000);
}

int main(){
 roundtrips();actual_material_contracts();fallback_and_alias_cases();failure_cases();malformed_contracts();shadow_reset_and_performance();
 std::printf("motion_wrap_states checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
