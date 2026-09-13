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
#include <stdexcept>
using DWORD=std::uint32_t; using UINT=unsigned; using HRESULT=int;
constexpr HRESULT S_OK=0,E_FAIL=-1,D3DERR_NOTFOUND=-2; constexpr DWORD FALSE=0,TRUE=1;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
using D3DSAMPLERSTATETYPE=unsigned;
constexpr unsigned D3DSAMP_SRGBTEXTURE=11,D3DSAMP_MIPFILTER=7,D3DSAMP_MIPMAPLODBIAS=8;
constexpr unsigned D3DRS_COLORWRITEENABLE1=190,D3DRS_COLORWRITEENABLE2=191;
unsigned releases=0, checks=0, failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"line=%d %s\n",__LINE__,#x);}} while(0)
const std::uint32_t* release_mask=nullptr;
void (*release_check)()=nullptr;
struct IUnknown {unsigned refs=1; void AddRef(){++refs;} void Release(){if(release_check)release_check();if(release_mask)CHECK(*release_mask==0);++releases;if(!--refs)delete this;} virtual ~IUnknown()=default;};
struct IDirect3DVertexShader9:IUnknown{}; struct IDirect3DPixelShader9:IUnknown{};
struct IDirect3DBaseTexture9:IUnknown{unsigned GetLevelCount(){return 3;}};
template<class T>void release(T*&p){auto* saved=p;p=nullptr;if(saved)saved->Release();}
template<class...T>void log(const char*,T...){}
namespace x3::temporal {enum class AgxDecode{gamma22,srgb,none};}
namespace renderer {
struct CameraState{};
struct LinearEmissionConfig {float gain=1;bool coverage=false;};
enum class LinearEmissionResult {Applied,UnsupportedShader,AllocationFailure};
bool linear_emission_config_valid(const LinearEmissionConfig& c){return std::isfinite(c.gain)&&c.gain>=0&&c.gain<=16;}
unsigned emission_transforms=0,emission_lookups=0;float emission_gain=0;bool emission_reject=false,emission_throw=false;
bool linear_emission_pair_reviewed(std::uint64_t vs,std::uint64_t ps){++emission_lookups;return vs==50&&(ps==60||ps==61);}
LinearEmissionResult linear_emission_pixel_variant(const std::uint32_t*p,std::size_t,const LinearEmissionConfig& c,std::vector<std::uint32_t>& words){++emission_transforms;CHECK(c.coverage);emission_gain=c.gain;if(emission_throw)throw std::bad_alloc();if(emission_reject)return LinearEmissionResult::AllocationFailure;if(*p!=60&&*p!=61)return LinearEmissionResult::UnsupportedShader;words={*p+300};return LinearEmissionResult::Applied;}
struct LinearMaterialConfig {float direct_gain=1,material_emissive_gain=1,lightmap_emissive_gain=1;};
enum class LinearMaterialResult{Applied,UnsupportedShader};
enum class MaterialMotionResult{Applied,UnsupportedShader};
enum class HdrTonemap{Agx,Identity};
struct MotionOutputProfile{}; MotionOutputProfile row;
struct HdrConfig{HdrTonemap tonemap=HdrTonemap::Agx;x3::temporal::AgxDecode decode=x3::temporal::AgxDecode::gamma22;};
bool linear_material_config_valid(const LinearMaterialConfig&c){return std::isfinite(c.direct_gain)&&c.direct_gain>=0&&c.direct_gain<=16;}
unsigned contract_lookups=0;
std::uint32_t linear_material_sampler_mask(std::uint64_t vs,std::uint64_t ps){++contract_lookups;return vs==10&&ps==20?15u:vs==30&&ps==40?31u:0u;}
bool reject_row=false,throw_transform=false,reject_transform=false;
const MotionOutputProfile* material_motion_vertex_row(std::uint64_t hash,std::size_t){return reject_row||hash>=50?nullptr:&row;}
const MotionOutputProfile* material_motion_pixel_row(std::uint64_t hash,std::size_t){return reject_row||hash>=50?nullptr:&row;}
bool material_motion_vertex_exports_depth(const MotionOutputProfile&,bool x){return x;}
bool material_motion_pixel_writes_depth(const MotionOutputProfile&,bool x){return x;}
unsigned motion_transforms=0, material_transforms=0;
MaterialMotionResult material_motion_vertex_variant(const std::uint32_t*p,std::size_t,std::vector<std::uint32_t>&o,bool){++motion_transforms;if(throw_transform)throw std::bad_alloc();if(reject_transform)return MaterialMotionResult::UnsupportedShader;o={p[0]+100};return MaterialMotionResult::Applied;}
MaterialMotionResult material_motion_pixel_variant(const std::uint32_t*p,std::size_t n,std::vector<std::uint32_t>&o,bool d){return material_motion_vertex_variant(p,n,o,d);}
LinearMaterialResult linear_material_vertex_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o,bool){++material_transforms;CHECK(p[0]==10||p[0]==20||p[0]==30||p[0]==40||p[0]==21);o={p[0]+200};return LinearMaterialResult::Applied;}
LinearMaterialResult linear_material_pixel_variant(const std::uint32_t*p,std::size_t n,const LinearMaterialConfig&c,std::vector<std::uint32_t>&o,bool d){return linear_material_vertex_variant(p,n,c,o,d);}
}
struct Pass {bool active=true;unsigned references(){return 0;} bool tonemap_active()const{return active;}void shutdown(){}void after_reset(HRESULT){}void before_reset(){}void bind(void*,void*){}};
struct History{void invalidate(){}};
namespace camera_state {void reset(){}}
struct MotionRoute {
 bool linear_material=false,vs_set=false,ps_set=false,write2_set=false,rt2_set=false,write_set=false,rt_set=false;
 bool vs_constants_set=false,ps_constants_set=false,jittered=true;
 DWORD saved_write1=15,saved_write2=15;
};
struct Counters{unsigned draws=0,restore_failures=0,material_bind_failures=0,mip_bias_game_writes=0,rs_resyncs=0,sb_resyncs=0;};
struct Device {
 std::vector<int> calls; std::vector<unsigned> failed_calls; unsigned ordinal=0;
 IDirect3DVertexShader9* bound_vs=nullptr;IDirect3DPixelShader9* bound_ps=nullptr;
 std::array<DWORD,5> srgb{};unsigned sampler_reads=0;int fail_sampler=-1;bool fail_combined_create=false,fail_motion_create=false,fail_get_vs=false,fail_get_ps=false;
 unsigned emission_creates=0,vs_creates=0;bool fail_emission_create=false,partial_emission_create=false,null_emission_create=false;
 bool fails(){++ordinal;for(auto index:failed_calls)if(ordinal==index)return true;return false;}
};
struct IDirect3DVertexBuffer9:IUnknown{};struct IDirect3DIndexBuffer9:IUnknown{};struct IDirect3DVertexDeclaration9:IUnknown{};struct IDirect3DSurface9:IUnknown{};
struct D3DVIEWPORT9{DWORD X=0,Y=0,Width=1,Height=1;float MinZ=0,MaxZ=1;};
struct Surface{bool known=false;};
struct Viewport{bool known=false;DWORD x=0,y=0,w=0,h=0;float low=0,high=1;};
Surface describe_surface(IDirect3DSurface9*){return {};}
bool same(const Surface&,const Surface&){return false;}
constexpr struct {unsigned count=1;unsigned base[1]={24};}matrix_windows;
template<class T>struct ThrowingMap:std::map<void*,T>{bool fail_next=false;T&operator[](void*key){if(fail_next){fail_next=false;throw std::bad_alloc();}return std::map<void*,T>::operator[](key);}};
using D=Device*;
using SetVsFn=HRESULT(*)(D,IDirect3DVertexShader9*);using SetPsFn=HRESULT(*)(D,IDirect3DPixelShader9*);
using CreateVsFn=HRESULT(*)(D,const DWORD*,IDirect3DVertexShader9**);using CreatePsFn=HRESULT(*)(D,const DWORD*,IDirect3DPixelShader9**);
using GetSamplerStateFn=HRESULT(*)(D,DWORD,D3DSAMPLERSTATETYPE,DWORD*);
using GetTextureFn=HRESULT(*)(D,DWORD,IDirect3DBaseTexture9**);
using SetRenderStateFn=HRESULT(*)(D,DWORD,DWORD);using SetConstantsFFn=HRESULT(*)(D,UINT,const float*,UINT);
using GetVsFn=HRESULT(*)(D,IDirect3DVertexShader9**);using GetPsFn=HRESULT(*)(D,IDirect3DPixelShader9**);
using GetConstantsFFn=HRESULT(*)(D,UINT,float*,UINT);using GetConstantsIFn=HRESULT(*)(D,UINT,int*,UINT);
using GetStreamFn=HRESULT(*)(D,UINT,IDirect3DVertexBuffer9**,UINT*,UINT*);using GetIndicesFn=HRESULT(*)(D,IDirect3DIndexBuffer9**);
using GetDeclarationFn=HRESULT(*)(D,IDirect3DVertexDeclaration9**);using GetRenderTargetFn=HRESULT(*)(D,DWORD,IDirect3DSurface9**);
using GetDepthFn=HRESULT(*)(D,IDirect3DSurface9**);using GetViewportFn=HRESULT(*)(D,D3DVIEWPORT9*);
enum Slots{SetVertexShader,SetPixelShader,CreateVertexShader,CreatePixelShader,GetSamplerState,GetTexture,SetRenderState,SetVertexShaderConstantF,SetPixelShaderConstantF,GetVertexShader,GetPixelShader,GetVertexShaderConstantF,GetVertexShaderConstantI,GetPixelShaderConstantF,GetStreamSource,GetIndices,GetVertexDeclaration,GetRenderTarget,GetDepthStencilSurface,GetViewport};
HRESULT set_vs(D d,IDirect3DVertexShader9*p){d->calls.push_back(1);if(d->fails())return E_FAIL;d->bound_vs=p;return S_OK;}
HRESULT set_ps(D d,IDirect3DPixelShader9*p){d->calls.push_back(2);if(d->fails())return E_FAIL;d->bound_ps=p;return S_OK;}
HRESULT create_vs(D d,const DWORD*p,IDirect3DVertexShader9**out){++d->vs_creates;if((*p>=200&&d->fail_combined_create)||(*p<200&&d->fail_motion_create))return E_FAIL;*out=new IDirect3DVertexShader9;return S_OK;}
HRESULT create_ps(D d,const DWORD*p,IDirect3DPixelShader9**out){if(*p>=300){++d->emission_creates;if(d->partial_emission_create){*out=new IDirect3DPixelShader9;return E_FAIL;}if(d->fail_emission_create)return E_FAIL;if(!d->null_emission_create)*out=new IDirect3DPixelShader9;return S_OK;}if((*p>=200&&d->fail_combined_create)||(*p<200&&d->fail_motion_create))return E_FAIL;*out=new IDirect3DPixelShader9;return S_OK;}
HRESULT get_sampler(D d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD*out){CHECK(type==D3DSAMP_SRGBTEXTURE);++d->sampler_reads;if(int(stage)==d->fail_sampler)return E_FAIL;*out=d->srgb[stage];return S_OK;}
HRESULT get_texture(D,DWORD,IDirect3DBaseTexture9**out){*out=nullptr;return S_OK;}
HRESULT set_state(D,DWORD,DWORD){return S_OK;}
HRESULT set_constants(D,UINT,const float*,UINT){return S_OK;}
HRESULT get_vs(D d,IDirect3DVertexShader9**p){if(d->fail_get_vs)return E_FAIL;*p=d->bound_vs;if(*p)(*p)->AddRef();return S_OK;}
HRESULT get_ps(D d,IDirect3DPixelShader9**p){if(d->fail_get_ps)return E_FAIL;*p=d->bound_ps;if(*p)(*p)->AddRef();return S_OK;}
HRESULT get_f(D,UINT,float*,UINT){return S_OK;}HRESULT get_i(D,UINT,int*,UINT){return S_OK;}
HRESULT get_stream(D,UINT,IDirect3DVertexBuffer9**p,UINT*,UINT*){*p=nullptr;return S_OK;}
HRESULT get_indices(D,IDirect3DIndexBuffer9**p){*p=nullptr;return S_OK;}
HRESULT get_declaration(D,IDirect3DVertexDeclaration9**p){*p=nullptr;return S_OK;}
HRESULT get_target(D,DWORD,IDirect3DSurface9**p){*p=nullptr;return D3DERR_NOTFOUND;}
HRESULT get_depth(D,IDirect3DSurface9**p){*p=nullptr;return D3DERR_NOTFOUND;}
HRESULT get_viewport(D,D3DVIEWPORT9*){return S_OK;}
class MotionOutput {
public:
 struct ShaderEntry {std::uint64_t hash=0;IUnknown*variant=nullptr,*material_variant=nullptr;IDirect3DPixelShader9*emission_variant=nullptr;bool registered=false;const renderer::MotionOutputProfile*row=nullptr;};
 struct Shadow {
 IDirect3DVertexShader9*vs=nullptr,*vs_variant=nullptr,*vs_material_variant=nullptr;
 IDirect3DPixelShader9*ps=nullptr,*ps_variant=nullptr,*ps_material_variant=nullptr;
 bool vs_registered=false,ps_registered=false;IDirect3DPixelShader9*ps_emission_variant=nullptr,*emission_eligible_variant=nullptr;
 std::uint64_t vs_hash=0,ps_hash=0;const renderer::MotionOutputProfile*vs_row=nullptr;
 std::uint32_t material_sampler_mask=0;float rows[1][16]{};bool rows_known[1]{};int integer0[4]{};bool integer0_known=false;
 Surface rt0,depth;Viewport viewport;bool extra_rt[4]{};
 bool recording=false,vs_reserved_written=false,ps_reserved_written=false;float vs_reserved[16]{},ps_reserved[8]{};
 }shadow_;
 struct SamplerShadow {IDirect3DBaseTexture9*texture=nullptr;DWORD levels=0,mipfilter=0,saved_bias=0,srgb=0;bool srgb_known=false,mipfilter_known=false,saved_known=false,biased=false;};
 static constexpr unsigned sampler_stage_count=16,failure_log_limit=8;
 SamplerShadow samplers_[16];
 ThrowingMap<ShaderEntry>vertex_,pixel_;
 D device_=nullptr;bool enabled_=true,requested_=true,depth_enabled_=true,linear_material_requested_=false;
 renderer::LinearMaterialConfig linear_material_config_{};
 bool linear_emission_requested_=false;renderer::LinearEmissionConfig linear_emission_config_{1,true};
 bool releasing_=false,taa_busy_=false,hdr_enabled_=true,fill_pending_=false;
 bool hdr_target_failed_=false,hdr_blocked_=false,target_failed_=false,pending_valid_=false,main_msaa_=false,msaa_logged_=false;unsigned hdr_blocked_latches_=0,main_msaa_samples_=0;Surface main_,main_depth_;History selector_;renderer::CameraState camera_previous_;
 unsigned taa_references_=0;std::unique_ptr<Pass>hdr_=std::make_unique<Pass>(),taa_;History history_;
 IUnknown*target_surface_=nullptr,*depth_surface_=nullptr,*sentinel_ps_=nullptr,*sentinel_mrt_ps_=nullptr,*quad_vs_=nullptr,*quad_declaration_=nullptr;
 enum class HdrState{Off,Active,Suspended};HdrState hdr_state_=HdrState::Active;renderer::HdrConfig hdr_config_;
 std::uint64_t id_=1,frame_=1,generation_=0;bool scene_open_=false;
 struct {unsigned NumSimultaneousRTs=3;}caps_;
 IDirect3DSurface9*hdr_main_=nullptr;Surface hdr_target_;Counters counters_;unsigned logged_failures_=0;bool states_invalidated=false;
 DWORD mip_bias_bits_=0,sampler_bound_mask_=0,sampler_biased_mask_=0;float mip_bias_=0;bool mip_bias_summary_logged_=false;
 unsigned mip_bias_total_sets_=0,mip_bias_total_restores_=0,mip_bias_total_reads_=0,mip_bias_total_game_writes_=0,mip_bias_total_failures_=0;
 DWORD mip_bias_game_write_stage_=0,mip_bias_game_write_value_=0;
 template<class F>F native(unsigned slot)const {
  switch(slot){case SetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_vs));case SetPixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_ps));
  case CreateVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(create_vs));case CreatePixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(create_ps));
  case GetSamplerState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_sampler));case GetTexture:return reinterpret_cast<F>(reinterpret_cast<void*>(get_texture));
  case GetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(get_vs));case GetPixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(get_ps));
  case GetVertexShaderConstantF:case GetPixelShaderConstantF:return reinterpret_cast<F>(reinterpret_cast<void*>(get_f));
  case GetVertexShaderConstantI:return reinterpret_cast<F>(reinterpret_cast<void*>(get_i));
  case GetStreamSource:return reinterpret_cast<F>(reinterpret_cast<void*>(get_stream));case GetIndices:return reinterpret_cast<F>(reinterpret_cast<void*>(get_indices));
  case GetVertexDeclaration:return reinterpret_cast<F>(reinterpret_cast<void*>(get_declaration));case GetRenderTarget:return reinterpret_cast<F>(reinterpret_cast<void*>(get_target));
  case GetDepthStencilSurface:return reinterpret_cast<F>(reinterpret_cast<void*>(get_depth));case GetViewport:return reinterpret_cast<F>(reinterpret_cast<void*>(get_viewport));
  case SetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(set_state));default:return reinterpret_cast<F>(reinterpret_cast<void*>(set_constants));}
 }
 void set_stream_source(UINT,IDirect3DVertexBuffer9*,UINT,UINT){}void set_indices(IDirect3DIndexBuffer9*){}void set_vertex_declaration(IDirect3DVertexDeclaration9*){}
 void begin_frame(std::uint64_t,bool){}void resync_shadow()noexcept;void after_reset(HRESULT)noexcept;
 void begin_stateblock()noexcept;void end_stateblock()noexcept;void stateblock_applied()noexcept;
 void restore_bindings(){}void before_reset()noexcept;
 void drop_redirect(){} void release_target(){release(target_surface_);release(depth_surface_);}
 template<class F>void taa_call(F&&f){f();}void invalidate_render_states(){states_invalidated=true;}
 HRESULT bind_target(unsigned,IUnknown*){return S_OK;}
 unsigned device_references()const noexcept;void release_resources()noexcept;
 void configure_linear_materials(bool,const renderer::LinearMaterialConfig&)noexcept;
 void configure_linear_emissions(bool,float)noexcept;void refresh_linear_emission_contract()noexcept;
 void register_vertex_shader(IDirect3DVertexShader9*,const DWORD*,std::size_t,std::uint64_t)noexcept;
 void register_pixel_shader(IDirect3DPixelShader9*,const DWORD*,std::size_t,std::uint64_t)noexcept;
 void set_vertex_shader(IDirect3DVertexShader9*)noexcept;void set_pixel_shader(IDirect3DPixelShader9*)noexcept;
 void set_sampler_state(DWORD,D3DSAMPLERSTATETYPE,DWORD)noexcept;void resync_samplers()noexcept;
 void refresh_linear_material_contract()noexcept;unsigned linear_material_refusal()const noexcept;HRESULT bind_variant_pair(MotionRoute&,bool)noexcept;HRESULT undo(MotionRoute&)noexcept;
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
// Cache lifecycle coverage uses real registration/setter/resync methods above.
void contract_lifecycle() {
 Device device;MotionOutput m;m.configure_linear_materials(true,{});m.device_=&device;
 IDirect3DVertexShader9 base_vs,bump_vs,unknown_vs;
 IDirect3DPixelShader9 base_ps,bump_ps,negative_ps,unknown_ps;
 DWORD base_v=10,base_p=20,bump_v=30,bump_p=40,negative_p=21;
 unsigned lookups=renderer::contract_lookups;
 m.register_vertex_shader(&base_vs,&base_v,4,10);m.register_pixel_shader(&base_ps,&base_p,4,20);
 m.register_vertex_shader(&bump_vs,&bump_v,4,30);m.register_pixel_shader(&bump_ps,&bump_p,4,40);
 m.register_pixel_shader(&negative_ps,&negative_p,4,21);
 CHECK(renderer::contract_lookups==lookups); // Unbound creation resolves no pair.
 auto select_default=[&]{device.bound_vs=&base_vs;device.bound_ps=&base_ps;m.set_vertex_shader(&base_vs);m.set_pixel_shader(&base_ps);};
 auto select_bump=[&]{device.bound_vs=&bump_vs;device.bound_ps=&bump_ps;m.set_vertex_shader(&bump_vs);m.set_pixel_shader(&bump_ps);};
 select_default();CHECK(m.shadow_.material_sampler_mask==15);
 device.fail_sampler=4;m.resync_samplers();CHECK(!m.samplers_[4].srgb_known&&m.linear_material_refusal()==0);
 select_bump();CHECK(m.shadow_.material_sampler_mask==31&&m.linear_material_refusal()==4);
 m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,TRUE);CHECK(m.linear_material_refusal()==4);
 select_default();CHECK(m.linear_material_refusal()==0); // S4 TRUE is irrelevant to DEFAULT.
 select_bump();m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,FALSE);CHECK(m.linear_material_refusal()==0);
 unsigned reads=device.sampler_reads;lookups=renderer::contract_lookups;
 for(unsigned n=0;n<1000;++n)CHECK(m.linear_material_refusal()==0);
 CHECK(device.sampler_reads==reads&&renderer::contract_lookups==lookups);
 // Null, unknown and same-VS unreviewed PS setters all revoke admission.
 m.set_pixel_shader(&negative_ps);CHECK(m.shadow_.vs==&bump_vs&&m.shadow_.material_sampler_mask==0);
 m.set_pixel_shader(&bump_ps);CHECK(m.shadow_.material_sampler_mask==31);
 m.set_vertex_shader(nullptr);CHECK(m.shadow_.material_sampler_mask==0);
 m.set_vertex_shader(&unknown_vs);CHECK(m.shadow_.material_sampler_mask==0);
 m.set_vertex_shader(&bump_vs);CHECK(m.shadow_.material_sampler_mask==31);
 m.set_pixel_shader(&unknown_ps);CHECK(m.shadow_.material_sampler_mask==0);
 m.set_pixel_shader(nullptr);CHECK(m.shadow_.material_sampler_mask==0);
 select_default();device.fail_sampler=-1;m.resync_samplers();
 // Recorded writes do not change active pair or S4. End/Apply pull actual
 // native state, including the current pair and five sampler decode values.
 m.begin_stateblock();lookups=renderer::contract_lookups;
 m.set_vertex_shader(&bump_vs);m.set_pixel_shader(&bump_ps);m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,TRUE);
 CHECK(m.shadow_.material_sampler_mask==15&&!m.samplers_[4].srgb&&renderer::contract_lookups==lookups);
 m.end_stateblock();CHECK(m.shadow_.material_sampler_mask==15&&m.linear_material_refusal()==0);
 device.bound_vs=&bump_vs;device.bound_ps=&bump_ps;device.srgb[4]=TRUE;m.stateblock_applied();
 CHECK(m.shadow_.material_sampler_mask==31&&m.linear_material_refusal()==4);
 device.srgb[4]=FALSE;m.stateblock_applied();CHECK(m.shadow_.material_sampler_mask==31&&m.linear_material_refusal()==0);
 // A failed shader getter during full resync cannot leave a stale contract.
 for(bool fail_vertex:{true,false}) {
  device.fail_get_vs=fail_vertex;device.fail_get_ps=!fail_vertex;m.stateblock_applied();CHECK(m.shadow_.material_sampler_mask==0&&m.linear_material_refusal()==1);
  device.fail_get_vs=device.fail_get_ps=false;m.stateblock_applied();CHECK(m.shadow_.material_sampler_mask==31);
 }
 // Successful Reset refreshes S4 and the pair without application setters.
 device.srgb[4]=TRUE;m.after_reset(S_OK);CHECK(m.shadow_.material_sampler_mask==31&&m.linear_material_refusal()==4);
 device.srgb[4]=FALSE;m.after_reset(S_OK);CHECK(m.shadow_.material_sampler_mask==31&&m.linear_material_refusal()==0);
 device.fail_get_vs=true;m.after_reset(S_OK);CHECK(m.shadow_.material_sampler_mask==0);device.fail_get_vs=false;
 m.after_reset(S_OK);CHECK(m.shadow_.material_sampler_mask==31);
 // Every bound-registration early exit/exception invalidates before releasing
 // old objects, and another stage's setter cannot resurrect that old mask.
 for(bool vertex:{true,false})for(unsigned failure=0;failure<9;++failure) {
  select_default();CHECK(m.shadow_.material_sampler_mask==15);
  renderer::reject_row=failure==2;renderer::throw_transform=failure==4;renderer::reject_transform=failure==5;
  device.fail_motion_create=failure==6;m.enabled_=failure!=7;m.requested_=failure!=8;
  if(failure==3){if(vertex)m.vertex_.fail_next=true;else m.pixel_.fail_next=true;}
  release_mask=&m.shadow_.material_sampler_mask;
  if(vertex)m.register_vertex_shader(&base_vs,failure==0?nullptr:&base_v,failure==1?3:4,10);
  else m.register_pixel_shader(&base_ps,failure==0?nullptr:&base_p,failure==1?3:4,20);
  CHECK(m.shadow_.material_sampler_mask==0);
  release_mask=nullptr;m.enabled_=m.requested_=true;
  if(vertex)m.set_pixel_shader(&base_ps);else m.set_vertex_shader(&base_vs);
  CHECK(m.shadow_.material_sampler_mask==0);
  renderer::reject_row=renderer::throw_transform=renderer::reject_transform=false;device.fail_motion_create=false;
  if(vertex)m.register_vertex_shader(&base_vs,&base_v,4,10);else m.register_pixel_shader(&base_ps,&base_p,4,20);
  CHECK(m.shadow_.material_sampler_mask==15&&m.linear_material_refusal()==0);
 }
 // Material Create failure keeps the ordinary registered pair; combined
 // readiness stays live, separate from the exact cached sampler contract.
 device.fail_combined_create=true;release_mask=&m.shadow_.material_sampler_mask;
 m.register_pixel_shader(&base_ps,&base_p,4,20);release_mask=nullptr;
 CHECK(m.shadow_.material_sampler_mask==15&&m.linear_material_refusal()==2);
 device.fail_combined_create=false;m.register_pixel_shader(&base_ps,&base_p,4,20);
 CHECK(m.linear_material_refusal()==0);
 m.release_resources();CHECK(m.device_references()==0&&m.shadow_.material_sampler_mask==0);
 // Even actual setters and resync issue no contract lookup when disabled.
 MotionOutput off;off.device_=&device;lookups=renderer::contract_lookups;reads=device.sampler_reads;
 off.register_vertex_shader(&base_vs,&base_v,4,10);off.register_pixel_shader(&base_ps,&base_p,4,20);
 off.set_vertex_shader(&base_vs);off.set_pixel_shader(&base_ps);off.resync_shadow();
 CHECK(renderer::contract_lookups==lookups&&device.sampler_reads==reads&&off.shadow_.material_sampler_mask==0);
 off.release_resources();
}

MotionOutput* retiring_emission=nullptr;
void emission_retirement_check(){CHECK(retiring_emission&&!retiring_emission->shadow_.emission_eligible_variant&&!retiring_emission->shadow_.ps_emission_variant);}
void emission_cache_cases(){
 const unsigned checks_before=checks;
 unsigned transforms=renderer::emission_transforms,lookups=renderer::emission_lookups;
 IDirect3DVertexShader9 vs,alias,unknown_vs;IDirect3DPixelShader9 ps,other,unknown_ps;DWORD v=50,p=60,q=61,negative=62;
 Device quiet;MotionOutput off;off.device_=&quiet;
 off.register_vertex_shader(&vs,&v,4,50);off.register_pixel_shader(&ps,&p,4,60);off.set_vertex_shader(&vs);off.set_pixel_shader(&ps);off.resync_shadow();
 CHECK(renderer::emission_transforms==transforms&&renderer::emission_lookups==lookups&&quiet.emission_creates==0&&!off.shadow_.emission_eligible_variant);off.release_resources();
 for(float gain:{-1.f,16.01f,NAN,INFINITY,-INFINITY}){MotionOutput bad;bad.configure_linear_emissions(true,gain);CHECK(!bad.linear_emission_requested_);}
 for(float gain:{0.f,1.f,4.f,16.f}){Device device;MotionOutput m;m.configure_linear_emissions(true,gain);m.device_=&device;
  m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);m.register_vertex_shader(&vs,&v,4,50);m.register_pixel_shader(&ps,&p,4,60);
  CHECK(m.shadow_.emission_eligible_variant&&renderer::emission_gain==gain&&m.linear_emission_config_.coverage);
  CHECK(!m.shadow_.vs_row&&!m.shadow_.vs_variant&&!m.shadow_.ps_variant&&device.vs_creates==0&&device.emission_creates==1&&m.device_references()==1);
  m.configure_linear_emissions(false,gain==0?16:0);CHECK(m.linear_emission_requested_&&m.linear_emission_config_.gain==gain);
  m.release_resources();CHECK(m.device_references()==0);
 }
 Device device;MotionOutput m;m.configure_linear_emissions(true,2);m.configure_linear_materials(true,{});m.device_=&device;
 m.register_vertex_shader(&vs,&v,4,50);m.register_vertex_shader(&alias,&v,4,51);m.register_pixel_shader(&ps,&p,4,60);m.register_pixel_shader(&other,&q,4,61);
 auto select=[&]{device.bound_vs=&vs;device.bound_ps=&ps;m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);};select();CHECK(m.shadow_.emission_eligible_variant&&m.device_references()==2);
 auto* first=m.shadow_.emission_eligible_variant;m.set_pixel_shader(&other);CHECK(m.shadow_.emission_eligible_variant&&m.shadow_.emission_eligible_variant!=first&&m.shadow_.vs==&vs);
 m.set_vertex_shader(&alias);CHECK(!m.shadow_.emission_eligible_variant);m.set_vertex_shader(&vs);CHECK(m.shadow_.emission_eligible_variant);
 m.register_pixel_shader(&unknown_ps,&negative,4,62);m.set_pixel_shader(&unknown_ps);CHECK(!m.shadow_.emission_eligible_variant);select();
 m.set_vertex_shader(nullptr);CHECK(!m.shadow_.emission_eligible_variant);m.set_vertex_shader(&unknown_vs);CHECK(!m.shadow_.emission_eligible_variant);select();m.set_pixel_shader(nullptr);CHECK(!m.shadow_.emission_eligible_variant);select();
 lookups=renderer::emission_lookups;transforms=renderer::emission_transforms;const unsigned creates=device.emission_creates,reads=device.sampler_reads;
 for(unsigned n=0;n<1000;++n)CHECK(m.shadow_.emission_eligible_variant==first);
 CHECK(renderer::emission_lookups==lookups&&renderer::emission_transforms==transforms&&device.emission_creates==creates&&device.sampler_reads==reads);
 m.begin_stateblock();m.set_vertex_shader(&alias);m.set_pixel_shader(&other);CHECK(m.shadow_.emission_eligible_variant==first&&renderer::emission_lookups==lookups);m.end_stateblock();CHECK(m.shadow_.emission_eligible_variant==first);
 device.bound_vs=&alias;m.stateblock_applied();CHECK(!m.shadow_.emission_eligible_variant);device.bound_vs=&vs;device.bound_ps=&other;m.stateblock_applied();CHECK(m.shadow_.emission_eligible_variant&&m.shadow_.emission_eligible_variant!=first);select();
 for(bool vertex:{true,false}){device.fail_get_vs=vertex;device.fail_get_ps=!vertex;m.stateblock_applied();CHECK(!m.shadow_.emission_eligible_variant);device.fail_get_vs=device.fail_get_ps=false;m.stateblock_applied();CHECK(m.shadow_.emission_eligible_variant);}
 const auto refs=m.device_references();auto* retained=m.pixel_[&ps].emission_variant;m.before_reset();CHECK(!m.shadow_.emission_eligible_variant&&m.device_references()==refs);m.after_reset(E_FAIL);CHECK(!m.shadow_.emission_eligible_variant);m.after_reset(S_OK);CHECK(m.shadow_.emission_eligible_variant==retained&&m.linear_emission_config_.gain==2);
 // Failed bound registration cannot survive another stage's successful setter.
 for(bool vertex:{true,false})for(unsigned failure=0;failure<6;++failure){select();CHECK(m.shadow_.emission_eligible_variant);
  m.enabled_=failure!=3;m.requested_=failure!=4;if(failure==5){if(vertex)m.vertex_.fail_next=true;else m.pixel_.fail_next=true;}
  if(vertex)m.register_vertex_shader(&vs,failure==0?nullptr:&v,failure==1?3:failure==2?0:4,50);
  else m.register_pixel_shader(&ps,failure==0?nullptr:&p,failure==1?3:failure==2?0:4,60);
  CHECK(!m.shadow_.emission_eligible_variant);m.enabled_=m.requested_=true;
  if(vertex)m.set_pixel_shader(&ps);else m.set_vertex_shader(&vs);CHECK(!m.shadow_.emission_eligible_variant);
  if(vertex)m.register_vertex_shader(&vs,&v,4,50);else m.register_pixel_shader(&ps,&p,4,60);CHECK(m.shadow_.emission_eligible_variant);
 }
 for(unsigned failure=0;failure<5;++failure){renderer::emission_reject=failure==0;renderer::emission_throw=failure==1;device.fail_emission_create=failure==2;device.partial_emission_create=failure==3;device.null_emission_create=failure==4;
  retiring_emission=&m;release_check=emission_retirement_check;const auto released=releases;m.register_pixel_shader(&ps,&p,4,60);release_check=nullptr;
  CHECK(!m.shadow_.emission_eligible_variant&&!m.pixel_[&ps].emission_variant&&m.device_references()==1);CHECK(releases==released+(failure==3?2:1));m.set_vertex_shader(&vs);CHECK(!m.shadow_.emission_eligible_variant);
  renderer::emission_reject=renderer::emission_throw=false;device.fail_emission_create=device.partial_emission_create=device.null_emission_create=false;m.register_pixel_shader(&ps,&p,4,60);CHECK(m.shadow_.emission_eligible_variant&&m.device_references()==2);
 }
 // Independent motion/material originals keep both established variants.
 IDirect3DVertexShader9 material_vs;IDirect3DPixelShader9 material_ps;DWORD mv=10,mp=20;m.register_vertex_shader(&material_vs,&mv,4,10);m.register_pixel_shader(&material_ps,&mp,4,20);
 CHECK(m.vertex_[&material_vs].variant&&m.vertex_[&material_vs].material_variant&&m.pixel_[&material_ps].variant&&m.pixel_[&material_ps].material_variant&&!m.pixel_[&material_ps].emission_variant&&m.device_references()==6);
 const auto released=releases;retiring_emission=&m;release_check=emission_retirement_check;m.release_resources();release_check=nullptr;retiring_emission=nullptr;
 CHECK(releases==released+6&&m.device_references()==0&&!m.shadow_.emission_eligible_variant);m.release_resources();CHECK(releases==released+6);
 std::printf("linear_emission_cache checks=%u\n",checks-checks_before);
}
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
 // Mip bias is zero: five native sampler reads still establish the contract.
 m.resync_samplers();CHECK(device.sampler_reads==5&&m.linear_material_refusal()==0);
 for(unsigned stage=0;stage<4;++stage){m.set_sampler_state(stage,D3DSAMP_SRGBTEXTURE,1);CHECK(m.linear_material_refusal()==4);m.set_sampler_state(stage,D3DSAMP_SRGBTEXTURE,0);}
 m.shadow_.recording=true;m.set_sampler_state(0,D3DSAMP_SRGBTEXTURE,1);m.shadow_.recording=false;CHECK(m.linear_material_refusal()==0);
 device.srgb[2]=1;m.resync_samplers();CHECK(m.linear_material_refusal()==4);device.srgb[2]=0;device.fail_sampler=1;m.resync_samplers();CHECK(!m.samplers_[1].srgb_known&&m.linear_material_refusal()==4);
 device.fail_sampler=-1;m.resync_samplers();CHECK(m.linear_material_refusal()==0);
 unsigned reads=device.sampler_reads,lookups=renderer::contract_lookups;for(unsigned i=0;i<1000;++i)CHECK(m.linear_material_refusal()==0);CHECK(device.sampler_reads==reads&&renderer::contract_lookups==lookups);
 m.set_pixel_shader(nullptr);CHECK(m.linear_material_refusal()==1);m.set_pixel_shader(&original_ps);CHECK(m.linear_material_refusal()==0);
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
 contract_lifecycle();
 emission_cache_cases();
 std::printf("linear_material_live checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
