// Scripted host doubles execute unchanged MotionOutput functions. No game data,
// driver calls or duplicated routing algorithm belongs in this fixture.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
#include <array>
#include <utility>
#include <string>
#include <cwchar>
using DWORD=std::uint32_t; using UINT=unsigned; using HRESULT=int;
constexpr HRESULT S_OK=0,E_FAIL=-1; constexpr DWORD FALSE=0;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
using D3DSAMPLERSTATETYPE=unsigned;
constexpr unsigned D3DSAMP_SRGBTEXTURE=11,D3DSAMP_MIPFILTER=7,D3DSAMP_MIPMAPLODBIAS=8;
constexpr unsigned D3DRS_COLORWRITEENABLE1=190,D3DRS_COLORWRITEENABLE2=191;
unsigned releases=0, checks=0, failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"line=%d %s\n",__LINE__,#x);}} while(0)
struct IUnknown {unsigned refs=1; void Release(){++releases;if(!--refs)delete this;} virtual ~IUnknown()=default;};
struct IDirect3DVertexShader9:IUnknown{}; struct IDirect3DPixelShader9:IUnknown{};
struct IDirect3DBaseTexture9:IUnknown{unsigned GetLevelCount(){return 3;}};
template<class T>void release(T*&p){auto* saved=p;p=nullptr;if(saved)saved->Release();}
template<class...T>void log(const char*,T...){}
namespace x3::temporal {enum class AgxDecode{gamma22,srgb,none};}
namespace renderer {
struct LinearMaterialConfig {float direct_gain=1,material_emissive_gain=1,lightmap_emissive_gain=1;};
enum class LinearMaterialResult{Applied,UnsupportedShader};
enum class MaterialMotionResult{Applied,UnsupportedShader};
enum class HdrTonemap{Agx,Identity};
struct MotionOutputProfile{}; MotionOutputProfile row;
struct HdrConfig{HdrTonemap tonemap=HdrTonemap::Agx;x3::temporal::AgxDecode decode=x3::temporal::AgxDecode::gamma22;};
bool linear_material_config_valid(const LinearMaterialConfig&c){return std::isfinite(c.direct_gain)&&c.direct_gain>=0&&c.direct_gain<=16;}
bool linear_material_pair_reviewed(std::uint64_t vs,std::uint64_t ps){return vs==10&&ps==20;}
const MotionOutputProfile* material_motion_vertex_row(std::uint64_t,std::size_t){return &row;}
const MotionOutputProfile* material_motion_pixel_row(std::uint64_t,std::size_t){return &row;}
bool material_motion_vertex_exports_depth(const MotionOutputProfile&,bool x){return x;}
bool material_motion_pixel_writes_depth(const MotionOutputProfile&,bool x){return x;}
unsigned motion_transforms=0, material_transforms=0;
MaterialMotionResult material_motion_vertex_variant(const std::uint32_t*p,std::size_t,std::vector<std::uint32_t>&o,bool){++motion_transforms;o={p[0]+100};return MaterialMotionResult::Applied;}
MaterialMotionResult material_motion_pixel_variant(const std::uint32_t*p,std::size_t n,std::vector<std::uint32_t>&o,bool d){return material_motion_vertex_variant(p,n,o,d);}
LinearMaterialResult linear_material_vertex_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o,bool){++material_transforms;CHECK(p[0]==10||p[0]==20);o={p[0]+200};return LinearMaterialResult::Applied;}
LinearMaterialResult linear_material_pixel_variant(const std::uint32_t*p,std::size_t n,const LinearMaterialConfig&c,std::vector<std::uint32_t>&o,bool d){return linear_material_vertex_variant(p,n,c,o,d);}
}
struct Pass {bool active=true;unsigned references(){return 0;} bool tonemap_active()const{return active;}void shutdown(){}};
struct History{void invalidate(){}};
struct MotionRoute {
 bool linear_material=false,vs_set=false,ps_set=false,write2_set=false,rt2_set=false,write_set=false,rt_set=false;
 bool vs_constants_set=false,ps_constants_set=false,jittered=true;
 DWORD saved_write1=15,saved_write2=15;
};
struct Counters{unsigned draws=0,restore_failures=0,material_bind_failures=0,mip_bias_game_writes=0;};
struct Device {
 std::vector<int> calls; std::vector<unsigned> failed_calls; unsigned ordinal=0;
 IDirect3DVertexShader9* bound_vs=nullptr;IDirect3DPixelShader9* bound_ps=nullptr;
 std::array<DWORD,4> srgb{};unsigned sampler_reads=0;int fail_sampler=-1;bool fail_combined_create=false;
 bool fails(){++ordinal;for(auto index:failed_calls)if(ordinal==index)return true;return false;}
};
using D=Device*;
using SetVsFn=HRESULT(*)(D,IDirect3DVertexShader9*);using SetPsFn=HRESULT(*)(D,IDirect3DPixelShader9*);
using CreateVsFn=HRESULT(*)(D,const DWORD*,IDirect3DVertexShader9**);using CreatePsFn=HRESULT(*)(D,const DWORD*,IDirect3DPixelShader9**);
using GetSamplerStateFn=HRESULT(*)(D,DWORD,D3DSAMPLERSTATETYPE,DWORD*);
using GetTextureFn=HRESULT(*)(D,DWORD,IDirect3DBaseTexture9**);
using SetRenderStateFn=HRESULT(*)(D,DWORD,DWORD);using SetConstantsFFn=HRESULT(*)(D,UINT,const float*,UINT);
enum Slots{SetVertexShader,SetPixelShader,CreateVertexShader,CreatePixelShader,GetSamplerState,GetTexture,SetRenderState,SetVertexShaderConstantF,SetPixelShaderConstantF};
HRESULT set_vs(D d,IDirect3DVertexShader9*p){d->calls.push_back(1);if(d->fails())return E_FAIL;d->bound_vs=p;return S_OK;}
HRESULT set_ps(D d,IDirect3DPixelShader9*p){d->calls.push_back(2);if(d->fails())return E_FAIL;d->bound_ps=p;return S_OK;}
HRESULT create_vs(D d,const DWORD*p,IDirect3DVertexShader9**out){if(*p>=200&&d->fail_combined_create)return E_FAIL;*out=new IDirect3DVertexShader9;return S_OK;}
HRESULT create_ps(D d,const DWORD*p,IDirect3DPixelShader9**out){if(*p>=200&&d->fail_combined_create)return E_FAIL;*out=new IDirect3DPixelShader9;return S_OK;}
HRESULT get_sampler(D d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD*out){CHECK(type==D3DSAMP_SRGBTEXTURE);++d->sampler_reads;if(int(stage)==d->fail_sampler)return E_FAIL;*out=d->srgb[stage];return S_OK;}
HRESULT get_texture(D,DWORD,IDirect3DBaseTexture9**out){*out=nullptr;return S_OK;}
HRESULT set_state(D,DWORD,DWORD){return S_OK;}
HRESULT set_constants(D,UINT,const float*,UINT){return S_OK;}
class MotionOutput {
public:
 struct ShaderEntry {std::uint64_t hash=0;IUnknown*variant=nullptr,*material_variant=nullptr;const renderer::MotionOutputProfile*row=nullptr;};
 struct Shadow {
 IDirect3DVertexShader9*vs=nullptr,*vs_variant=nullptr,*vs_material_variant=nullptr;
 IDirect3DPixelShader9*ps=nullptr,*ps_variant=nullptr,*ps_material_variant=nullptr;
 std::uint64_t vs_hash=0,ps_hash=0;const renderer::MotionOutputProfile*vs_row=nullptr;
 bool recording=false,vs_reserved_written=false,ps_reserved_written=false;float vs_reserved[16]{},ps_reserved[8]{};
 }shadow_;
 struct SamplerShadow {IDirect3DBaseTexture9*texture=nullptr;DWORD levels=0,mipfilter=0,saved_bias=0,srgb=0;bool srgb_known=false,mipfilter_known=false,saved_known=false,biased=false;};
 static constexpr unsigned sampler_stage_count=16,failure_log_limit=8;
 SamplerShadow samplers_[16];
 std::map<void*,ShaderEntry>vertex_,pixel_;
 D device_=nullptr;bool enabled_=true,requested_=true,depth_enabled_=true,linear_material_requested_=false;
 renderer::LinearMaterialConfig linear_material_config_{};
 bool releasing_=false,taa_busy_=false,hdr_enabled_=true,fill_pending_=false;
 unsigned taa_references_=0;std::unique_ptr<Pass>hdr_=std::make_unique<Pass>(),taa_;History history_;
 IUnknown*target_surface_=nullptr,*depth_surface_=nullptr,*sentinel_ps_=nullptr,*sentinel_mrt_ps_=nullptr,*quad_vs_=nullptr,*quad_declaration_=nullptr;
 enum class HdrState{Off,Active,Suspended};HdrState hdr_state_=HdrState::Active;renderer::HdrConfig hdr_config_;
 std::uint64_t id_=1,frame_=1;Counters counters_;unsigned logged_failures_=0;bool states_invalidated=false;
 DWORD mip_bias_bits_=0,sampler_bound_mask_=0,sampler_biased_mask_=0;float mip_bias_=0;bool mip_bias_summary_logged_=false;
 unsigned mip_bias_total_sets_=0,mip_bias_total_restores_=0,mip_bias_total_reads_=0,mip_bias_total_game_writes_=0,mip_bias_total_failures_=0;
 DWORD mip_bias_game_write_stage_=0,mip_bias_game_write_value_=0;
 template<class F>F native(unsigned slot)const {
  switch(slot){case SetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_vs));case SetPixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_ps));
  case CreateVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(create_vs));case CreatePixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(create_ps));
  case GetSamplerState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_sampler));case GetTexture:return reinterpret_cast<F>(reinterpret_cast<void*>(get_texture));
  case SetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(set_state));default:return reinterpret_cast<F>(reinterpret_cast<void*>(set_constants));}
 }
 void drop_redirect(){} void release_target(){release(target_surface_);release(depth_surface_);}
 template<class F>void taa_call(F&&f){f();}void invalidate_render_states(){states_invalidated=true;}
 HRESULT bind_target(unsigned,IUnknown*){return S_OK;}
 unsigned device_references()const noexcept;void release_resources()noexcept;
 void configure_linear_materials(bool,const renderer::LinearMaterialConfig&)noexcept;
 void register_vertex_shader(IDirect3DVertexShader9*,const DWORD*,std::size_t,std::uint64_t)noexcept;
 void register_pixel_shader(IDirect3DPixelShader9*,const DWORD*,std::size_t,std::uint64_t)noexcept;
 void set_vertex_shader(IDirect3DVertexShader9*)noexcept;void set_pixel_shader(IDirect3DPixelShader9*)noexcept;
 void set_sampler_state(DWORD,D3DSAMPLERSTATETYPE,DWORD)noexcept;void resync_samplers()noexcept;
 unsigned linear_material_refusal()const noexcept;HRESULT bind_variant_pair(MotionRoute&,bool)noexcept;HRESULT undo(MotionRoute&)noexcept;
};
// Win32 environment semantics needed by capture's unmodified parsing block.
std::map<std::wstring,std::wstring> environment;
constexpr DWORD ERROR_SUCCESS=0,ERROR_ENVVAR_NOT_FOUND=203;
DWORD last_error=0;
void SetLastError(DWORD value){last_error=value;}DWORD GetLastError(){return last_error;}
DWORD GetEnvironmentVariableW(const wchar_t*name,wchar_t*out,DWORD size){
 auto it=environment.find(name);if(it==environment.end()){last_error=ERROR_ENVVAR_NOT_FOUND;return 0;}
 if(it->second.size()>=size)return DWORD(it->second.size()+1);
 std::wmemcpy(out,it->second.c_str(),it->second.size()+1);return DWORD(it->second.size());
}
namespace x3m {namespace renderer=::renderer;}
bool linear_material_requested=false,motion_output_requested=true,hdr_requested=true;
renderer::LinearMaterialConfig linear_material_config;
renderer::HdrConfig hdr_config;
#include "linear_material_live_under_test_inc.h"
int main(){
 environment[L"X3M_HDR_TONEMAP"]=L"agx";environment[L"X3M_LINEAR_MATERIALS"]=L"1";configure_environment();CHECK(linear_material_requested&&linear_material_config.direct_gain==1);
 for(const wchar_t*key:{L"X3M_MATERIAL_DIRECT_GAIN",L"X3M_MATERIAL_EMISSIVE_GAIN",L"X3M_LIGHTMAP_EMISSIVE_GAIN"}){
  for(const wchar_t*bad:{L"",L"nan",L"inf",L"-inf",L"-1",L"16.01",L"abc",L"1x",L"1 ",L"1111111111111111111111111111111111"}){environment[key]=bad;configure_environment();CHECK(!linear_material_requested);}
  for(const wchar_t*good:{L"0",L"1",L"4",L"16"}){environment[key]=good;configure_environment();CHECK(linear_material_requested);}
  environment.erase(key);
 }
 for(const wchar_t*bad:{L"none",L"srgb",L"unknown",L"gamma2.2xxxxxxxxxxxxxxxxxxxxxxxxxxxx"}){environment[L"X3M_HDR_DECODE"]=bad;configure_environment();CHECK(!linear_material_requested);}
 for(const wchar_t*good:{L"gamma2.2",L"pow22",L"gamma"}){environment[L"X3M_HDR_DECODE"]=good;configure_environment();CHECK(linear_material_requested);}
 environment.erase(L"X3M_HDR_DECODE");
 for(const wchar_t*bad:{L"",L"identity",L"unknown",L"agxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}){environment[L"X3M_HDR_TONEMAP"]=bad;configure_environment();CHECK(!linear_material_requested);}
 environment[L"X3M_HDR_TONEMAP"]=L"1";configure_environment();CHECK(linear_material_requested);
 environment[L"X3M_HDR_TONEMAP"]=L"agx";hdr_requested=false;configure_environment();CHECK(!linear_material_requested);hdr_requested=true;
 motion_output_requested=false;configure_environment();CHECK(!linear_material_requested);motion_output_requested=true;
 hdr_config.tonemap=renderer::HdrTonemap::Identity;configure_environment();CHECK(!linear_material_requested);hdr_config.tonemap=renderer::HdrTonemap::Agx;
 environment[L"X3M_LINEAR_MATERIALS"]=L"0";configure_environment();CHECK(!linear_material_requested);

 Device device;MotionOutput m;m.configure_linear_materials(true,{});m.device_=&device;
 IDirect3DVertexShader9 original_vs;IDirect3DPixelShader9 original_ps;DWORD vs=10,ps=20;
 m.set_vertex_shader(&original_vs);m.set_pixel_shader(&original_ps);
 m.register_vertex_shader(&original_vs,&vs,4,10);m.register_pixel_shader(&original_ps,&ps,4,20);
 CHECK(renderer::motion_transforms==2&&renderer::material_transforms==2);
 CHECK(m.shadow_.vs_variant&&m.shadow_.vs_material_variant&&m.shadow_.ps_variant&&m.shadow_.ps_material_variant);
 CHECK(m.device_references()==4);
 auto original_gain=m.linear_material_config_.direct_gain;m.configure_linear_materials(false,{4,1,1});
 CHECK(m.linear_material_requested_&&m.linear_material_config_.direct_gain==original_gain);
 // Mip bias is zero: four native sampler reads still establish the contract.
 m.resync_samplers();CHECK(device.sampler_reads==4&&m.linear_material_refusal()==0);
 for(unsigned stage=0;stage<4;++stage){m.set_sampler_state(stage,D3DSAMP_SRGBTEXTURE,1);CHECK(m.linear_material_refusal()==4);m.set_sampler_state(stage,D3DSAMP_SRGBTEXTURE,0);}
 m.shadow_.recording=true;m.set_sampler_state(0,D3DSAMP_SRGBTEXTURE,1);m.shadow_.recording=false;CHECK(m.linear_material_refusal()==0);
 device.srgb[2]=1;m.resync_samplers();CHECK(m.linear_material_refusal()==4);device.srgb[2]=0;device.fail_sampler=1;m.resync_samplers();CHECK(!m.samplers_[1].srgb_known&&m.linear_material_refusal()==4);
 device.fail_sampler=-1;m.resync_samplers();CHECK(m.linear_material_refusal()==0);
 unsigned reads=device.sampler_reads;for(unsigned i=0;i<1000;++i)CHECK(m.linear_material_refusal()==0);CHECK(device.sampler_reads==reads);
 m.shadow_.ps_hash=21;CHECK(m.linear_material_refusal()==1);m.shadow_.ps_hash=20;
 m.hdr_state_=MotionOutput::HdrState::Suspended;CHECK(m.linear_material_refusal()==3);m.hdr_state_=MotionOutput::HdrState::Active;
 m.hdr_->active=false;CHECK(m.linear_material_refusal()==3);m.hdr_->active=true;
 m.hdr_config_.decode=x3::temporal::AgxDecode::srgb;CHECK(m.linear_material_refusal()==3);m.hdr_config_.decode=x3::temporal::AgxDecode::gamma22;
 // Successful combined binding, then exact original restoration.
 MotionRoute route;CHECK(m.bind_variant_pair(route,true)==S_OK&&route.linear_material&&route.jittered);
 CHECK(device.bound_vs==m.shadow_.vs_material_variant&&device.bound_ps==m.shadow_.ps_material_variant);
 CHECK(m.undo(route)==S_OK&&device.bound_vs==&original_vs&&device.bound_ps==&original_ps);
 // Each new-stage failure restores partial setup and retries motion once.
 for(unsigned failure:{1u,2u}){device.ordinal=0;device.calls.clear();device.failed_calls={failure};route={};CHECK(m.bind_variant_pair(route,true)==S_OK);CHECK(!route.linear_material&&route.jittered&&route.vs_set&&route.ps_set);CHECK(device.bound_vs==m.shadow_.vs_variant&&device.bound_ps==m.shadow_.ps_variant);CHECK(device.calls.size()==(failure==1?3:5));CHECK(m.undo(route)==S_OK);}
 // Failed partial restoration refuses retry, invalidates state and counts it.
 device.ordinal=0;device.calls.clear();device.failed_calls={2,3};route={};CHECK(m.bind_variant_pair(route,true)==E_FAIL);CHECK(device.calls.size()==3&&!route.linear_material&&m.states_invalidated&&m.counters_.restore_failures==1);
 // The single ordinary retry can itself fail; no recursive material attempt.
 device.ordinal=0;device.calls.clear();device.failed_calls={1,3};route={};CHECK(m.bind_variant_pair(route,true)==E_FAIL);CHECK(device.calls.size()==3&&route.vs_set&&!route.ps_set);device.failed_calls.clear();CHECK(m.undo(route)==S_OK);
 // Failed combined creation preserves ordinary motion, including the bound
 // registry shadow; re-registration releases both old objects exactly once.
 unsigned before=releases;device.fail_combined_create=true;m.register_vertex_shader(&original_vs,&vs,4,10);CHECK(releases==before+2);CHECK(m.shadow_.vs_variant&&!m.shadow_.vs_material_variant&&m.device_references()==3&&m.linear_material_refusal()==2);
 before=releases;m.release_resources();CHECK(releases==before+3&&m.device_references()==0);CHECK(!m.shadow_.vs_variant&&!m.shadow_.vs_material_variant&&!m.shadow_.ps_variant&&!m.shadow_.ps_material_variant);
 before=releases;m.release_resources();CHECK(releases==before);
 // Default off creates only motion and never performs material sampler reads.
 MotionOutput off;Device quiet;off.device_=&quiet;off.register_vertex_shader(&original_vs,&vs,4,10);CHECK(!off.vertex_[&original_vs].material_variant);off.resync_samplers();CHECK(quiet.sampler_reads==0);off.release_resources();
 std::printf("linear_material_live checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
