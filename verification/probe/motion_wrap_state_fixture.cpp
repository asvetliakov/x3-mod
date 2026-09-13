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
constexpr HRESULT S_OK=0,E_FAIL=-1,D3DERR_INVALIDCALL=-2;
constexpr bool SUCCEEDED(HRESULT h){return h>=0;} constexpr bool FAILED(HRESULT h){return h<0;}
using D3DRENDERSTATETYPE=unsigned;
constexpr unsigned D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_ALPHABLENDENABLE=27,
 D3DRS_COLORWRITEENABLE=168,D3DRS_SRGBWRITEENABLE=194,D3DRS_COLORWRITEENABLE1=190,D3DRS_COLORWRITEENABLE2=191,
 D3DRS_WRAP0=128,D3DRS_WRAP1=129,D3DRS_WRAP2=130,D3DRS_WRAP3=131,D3DRS_WRAP4=132,D3DRS_WRAP5=133,D3DRS_WRAP6=134,D3DRS_WRAP7=135,
 D3DRS_WRAP8=198,D3DRS_WRAP9=199,D3DRS_WRAP10=200,D3DRS_WRAP11=201,D3DRS_WRAP12=202,D3DRS_WRAP13=203,D3DRS_WRAP14=204,D3DRS_WRAP15=205;
constexpr unsigned motion_shadow_state_count=24,failure_log_limit=16;
namespace renderer {struct MotionOutputProfile{std::uint8_t texcoord_index=4,depth_texcoord_index=7;};}
struct MotionRoute {
 bool depth=false,write2_set=false,rt2_set=false,write_set=false,rt_set=false,ps_set=false,vs_set=false,vs_constants_set=false,ps_constants_set=false;
 DWORD saved_write1=15,saved_write2=15,saved_wrap[2]{};std::uint8_t wrap_index[2]{},wrap_count=0,wrap_attempted=0;
};
static unsigned failures=0,checks=0,allocations=0;
void* operator new(std::size_t n){++allocations;if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u: %s\n",__LINE__,#x);}}while(0)
struct Device {
 std::array<DWORD,256> states{};unsigned gets=0,sets=0;
 std::array<unsigned,3> fail_get{},fail_set{};
 bool mutation_before_failure=false;
 std::array<std::pair<unsigned,DWORD>,32> writes{};
};
using GetRenderStateFn=HRESULT(*)(Device*,D3DRENDERSTATETYPE,DWORD*);
using SetRenderStateFn=HRESULT(*)(Device*,D3DRENDERSTATETYPE,DWORD);
using SetPsFn=HRESULT(*)(Device*,void*);using SetVsFn=SetPsFn;
using SetConstantsFFn=HRESULT(*)(Device*,unsigned,const float*,unsigned);
enum{GetRenderState,SetRenderState,SetPixelShader,SetVertexShader,SetVertexShaderConstantF,SetPixelShaderConstantF};
HRESULT get_state(Device*d,unsigned s,DWORD*v){++d->gets;for(auto n:d->fail_get)if(n==d->gets)return E_FAIL;*v=d->states[s];return S_OK;}
HRESULT set_state(Device*d,unsigned s,DWORD v){++d->sets;d->writes[(d->sets-1)%32]={s,v};bool fail=false;for(auto n:d->fail_set)fail|=n==d->sets;if(!fail||d->mutation_before_failure)d->states[s]=v;return fail?E_FAIL:S_OK;}
HRESULT set_shader(Device*,void*){return S_OK;} HRESULT set_constants(Device*,unsigned,const float*,unsigned){return S_OK;}
template<class...A>void log(const char*,A...){ }
struct Pass {void after_reset(HRESULT){}};
class MotionOutput {
public:
 Device*device_;bool enabled_=true,state_shadow_=true,motion_state_lost_=false,scene_open_=false;
 HRESULT motion_state_error_=D3DERR_INVALIDCALL;
 unsigned id_=1,generation_=0,frame_=0,taa_references_=0,logged_failures_=0,taa_invalidations=0,frames=0;
 Pass*taa_=nullptr;
 struct{unsigned rs_queries=0,rs_hits=0,rs_gets=0,rs_resyncs=0,restore_failures=0,draws=0,sb_resyncs=0;}counters_;
 struct{DWORD states[motion_shadow_state_count]{};bool states_known[motion_shadow_state_count]{};bool recording=false;
  bool vs_reserved_written=false,ps_reserved_written=false;void*vs=nullptr,*ps=nullptr;float vs_reserved[16]{},ps_reserved[8]{};
 }shadow_;
 explicit MotionOutput(Device&d):device_(&d){}
 template<class F> F native(unsigned n){switch(n){case GetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_state));case SetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(set_state));
 case SetPixelShader:case SetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_shader));default:return reinterpret_cast<F>(reinterpret_cast<void*>(set_constants));}}
 HRESULT bind_target(unsigned,void*){return S_OK;}
 void invalidate_taa(){++taa_invalidations;}
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
 void after_reset(HRESULT) noexcept;
 void begin_stateblock() noexcept;void end_stateblock() noexcept;void stateblock_applied() noexcept;
};
#include "motion_wrap_under_test_inc.h"
unsigned wrap(unsigned i){return i<8?D3DRS_WRAP0+i:D3DRS_WRAP8+i-8;}
void roundtrips(){
 for(bool cached:{false,true})for(bool depth:{false,true})for(unsigned a=0;a<16;++a)for(unsigned b=0;b<16;++b){
  if(a==b)continue;Device d;MotionOutput m(d);m.state_shadow_=cached;
  for(unsigned i=0;i<16;++i){d.states[wrap(i)]=i+1;m.set_render_state(wrap(i),i+1);CHECK(shadow_index(wrap(i))==i+8);}
  const auto before=d.states;MotionRoute r;r.depth=depth;renderer::MotionOutputProfile p{std::uint8_t(a),std::uint8_t(b)};
  CHECK(m.apply_wrap_states(r,p)==S_OK);for(unsigned i=0;i<16;++i)CHECK(d.states[wrap(i)]==((i==a||(depth&&i==b))?0:i+1));
  CHECK(m.undo(r)==S_OK&&d.states==before&&!m.motion_state_lost_);CHECK(d.sets==(depth?4u:2u));CHECK(d.gets==(cached?0u:depth?2u:1u));
 }
}
void failure_cases(){
 renderer::MotionOutputProfile p{0,15};
 // A zero motion or depth state is neither overwritten nor restored.
 for(unsigned zero:{0u,15u}){Device d;d.states[wrap(0)]=5;d.states[wrap(15)]=9;d.states[wrap(zero)]=0;auto before=d.states;MotionOutput m(d);MotionRoute r;r.depth=true;
  CHECK(m.apply_wrap_states(r,p)==S_OK&&d.sets==1);CHECK(m.undo(r)==S_OK&&d.sets==2&&d.states==before);}
 // Profile corruption is refused before any state mutation.
 for(unsigned bad:{0u,1u}){Device d;MotionOutput m(d);MotionRoute r;r.depth=true;auto invalid=p;if(bad)invalid.depth_texcoord_index=255;else invalid.texcoord_index=16;
  CHECK(m.apply_wrap_states(r,invalid)==D3DERR_INVALIDCALL&&m.undo(r)==S_OK&&d.sets==0);}
 // Failed first/second read changes no state, and a later retry reads again.
 for(unsigned n:{1u,2u}){Device d;d.states[wrap(0)]=15;d.states[wrap(15)]=7;auto before=d.states;MotionOutput m(d);MotionRoute r;r.depth=true;d.fail_get[0]=n;
  CHECK(FAILED(m.apply_wrap_states(r,p)));CHECK(m.undo(r)==S_OK&&d.states==before&&d.sets==0&&!m.motion_state_lost_);d.fail_get={};r.depth=true;CHECK(m.apply_wrap_states(r,p)==S_OK);CHECK(m.undo(r)==S_OK&&d.states==before);}
 // Failed zeroing may have mutated device state. Both outcomes restore exactly.
 for(bool mutation:{false,true})for(unsigned n:{1u,2u}){Device d;d.states[wrap(0)]=15;d.states[wrap(15)]=7;auto before=d.states;d.fail_set[0]=n;d.mutation_before_failure=mutation;MotionOutput m(d);MotionRoute r;r.depth=true;
  CHECK(FAILED(m.apply_wrap_states(r,p)));CHECK(m.undo(r)==S_OK&&d.states==before&&!m.motion_state_lost_);CHECK(d.sets==n*2);}
 // Failed restores continue remaining rollback, latch and invalidate. Failed
 // Reset and failed post-Reset read both retain quarantine, even across frames.
 for(unsigned n:{3u,4u}){Device d;d.states[wrap(0)]=15;d.states[wrap(15)]=7;d.fail_set[0]=n;MotionOutput m(d);MotionRoute r;r.depth=true;
  CHECK(m.apply_wrap_states(r,p)==S_OK);CHECK(FAILED(m.undo(r))&&m.motion_state_lost_&&m.motion_state_error_==E_FAIL);CHECK(d.sets==4&&m.counters_.restore_failures==1&&m.taa_invalidations==1);
  CHECK(m.undo(r)==S_OK&&d.sets==4);m.after_reset(E_FAIL);CHECK(m.motion_state_lost_);d.fail_get[0]=d.gets+4;m.after_reset(S_OK);CHECK(m.motion_state_lost_);
  d.fail_get={};d.states={};m.after_reset(S_OK);CHECK(!m.motion_state_lost_&&m.motion_state_error_==D3DERR_INVALIDCALL);}
}
void shadow_cases(){
 Device d;MotionOutput m(d);renderer::MotionOutputProfile p{7,8};MotionRoute r;r.depth=true;
 CHECK(m.apply_wrap_states(r,p)==S_OK&&m.undo(r)==S_OK&&d.gets==2&&d.sets==0);
 auto gets=d.gets;CHECK(m.apply_wrap_states(r,p)==S_OK&&m.undo(r)==S_OK&&d.gets==gets&&d.sets==0);
 // Application writes and recorded setters have different actual-device effects.
 set_state(&d,wrap(7),5);m.set_render_state(wrap(7),5);m.begin_stateblock();m.set_render_state(wrap(7),9);CHECK(m.shadow_.states[15]==5);m.end_stateblock();
 CHECK(m.apply_wrap_states(r,p)==S_OK&&d.states[wrap(7)]==0);CHECK(m.undo(r)==S_OK&&d.states[wrap(7)]==5);
 d.states[wrap(7)]=12;d.states[wrap(8)]=3;m.stateblock_applied();CHECK(m.apply_wrap_states(r,p)==S_OK);CHECK(m.undo(r)==S_OK&&d.states[wrap(7)]==12&&d.states[wrap(8)]==3);
 // Shadow-off must see a device value despite a deliberately stale cache.
 m.state_shadow_=false;d.states[wrap(7)]=6;CHECK(m.apply_wrap_states(r,p)==S_OK);CHECK(m.undo(r)==S_OK&&d.states[wrap(7)]==6);
 // Zero steady-state work has no native getters/setters and no allocations.
 m.state_shadow_=true;d.states={};m.invalidate_render_states();r.depth=true;CHECK(m.apply_wrap_states(r,p)==S_OK&&m.undo(r)==S_OK);
 const auto alloc=allocations,reads=d.gets,writes=d.sets;const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<200000;++i){MotionRoute t;t.depth=true;m.apply_wrap_states(t,p);m.undo(t);}
 auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
 CHECK(allocations==alloc&&d.gets==reads&&d.sets==writes);std::printf("wrap_host_zero iterations=200000 ns_per_transaction=%.2f native_gets=0 native_sets=0 allocations=0\n",double(ns)/200000);
}
int main(){roundtrips();failure_cases();shadow_cases();std::printf("motion_wrap_states checks=%u failures=%u\n",checks,failures);return failures?1:0;}
