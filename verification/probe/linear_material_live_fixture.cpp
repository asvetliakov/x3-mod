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
#include <cstring>
using D3DFORMAT=unsigned;constexpr unsigned D3DFMT_UNKNOWN=0;
using DWORD=std::uint32_t; using UINT=unsigned; using HRESULT=int;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1,D3DERR_NOTFOUND=-2,D3DERR_INVALIDCALL=-3,E_NOINTERFACE=-4,E_OUTOFMEMORY=-5; constexpr DWORD FALSE=0,TRUE=1;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
using D3DSAMPLERSTATETYPE=unsigned;using D3DRENDERSTATETYPE=unsigned;
constexpr unsigned D3DZB_TRUE=1,D3DBLEND_SRCALPHA=5,D3DBLEND_INVSRCALPHA=6,D3DBLENDOP_ADD=1,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_BLENDOP=171,D3DRS_SEPARATEALPHABLENDENABLE=206,D3DRS_SRCBLENDALPHA=207,D3DRS_DESTBLENDALPHA=208,D3DRS_BLENDOPALPHA=209,D3DRS_BLENDFACTOR=193;
constexpr unsigned D3DSAMP_SRGBTEXTURE=11,D3DSAMP_MIPFILTER=7,D3DSAMP_MIPMAPLODBIAS=8;
constexpr unsigned D3DDEVCAPS2_DMAPNPATCH=1;
constexpr unsigned D3DDMAPSAMPLER=256,D3DVERTEXTEXTURESAMPLER0=257,D3DVERTEXTEXTURESAMPLER3=260;
constexpr unsigned IID_IUnknown=1,IID_IDirect3DTexture9=2;
constexpr unsigned D3DRS_COLORWRITEENABLE1=190,D3DRS_COLORWRITEENABLE2=191;
constexpr unsigned D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_ALPHABLENDENABLE=27,D3DRS_COLORWRITEENABLE=168,D3DRS_SRGBWRITEENABLE=194;
constexpr unsigned D3DRS_WRAP0=128,D3DRS_WRAP7=135,D3DRS_WRAP8=198,D3DRS_WRAP15=205;
constexpr unsigned D3DRS_FILLMODE=8;
// Step C packed-screen state: the native screen blend and the projected-TSS gate.
constexpr unsigned D3DRS_DITHERENABLE=26,D3DBLEND_ONE=2,D3DBLEND_INVSRCCOLOR=4;
constexpr unsigned D3DTSS_TEXTURETRANSFORMFLAGS=24,D3DTTFF_PROJECTED=256;
struct RECT{long left=0,top=0,right=0,bottom=0;};
constexpr unsigned motion_shadow_state_count=24;
constexpr std::array<unsigned,24>shadow_states{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,128,129,130,131,132,133,134,135,198,199,200,201,202,203,204,205};
// Mirrors the production blend shadow table (motion_output.cpp): the fade
// check's triple plus SEPARATEALPHABLENDENABLE, then the separate alpha triple
// that only the source-gain refusal lines read.
constexpr unsigned composition_blend_count=8;
constexpr D3DRENDERSTATETYPE composition_blend_states[composition_blend_count]={
 D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,
 D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_BLENDFACTOR};
unsigned releases=0, checks=0, failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"line=%d %s\n",__LINE__,#x);}} while(0)
const std::uint32_t* release_mask=nullptr;
const bool* release_bump=nullptr;
void (*release_check)()=nullptr;
struct IUnknown {unsigned refs=1; void AddRef(){++refs;} void Release(){if(release_check)release_check();if(release_mask){CHECK(*release_mask==0);if(release_bump)CHECK(!*release_bump);}++releases;if(!--refs)delete this;} virtual ~IUnknown()=default;};
struct IDirect3DVertexShader9:IUnknown{}; struct IDirect3DPixelShader9:IUnknown{};
struct IDirect3DBaseTexture9:IUnknown{IUnknown*identity=this;bool fail_identity=false,partial_identity=false;unsigned identity_queries=0;unsigned GetLevelCount(){return 3;}HRESULT QueryInterface(unsigned,void**out){++identity_queries;if(fail_identity){if(partial_identity){*out=identity;if(identity)identity->AddRef();}return E_FAIL;}*out=identity;if(identity)identity->AddRef();return S_OK;}};
struct IDirect3DTexture9:IDirect3DBaseTexture9{};
struct IDirect3DSurface9:IUnknown{bool describable=true;IDirect3DTexture9*texture=nullptr;HRESULT GetContainer(unsigned,void**out){*out=texture;if(texture)texture->AddRef();return texture?S_OK:E_FAIL;}};
template<class T>void release(T*&p){auto* saved=p;p=nullptr;if(saved)saved->Release();}
struct XtNoticeLog {unsigned calls=0;std::uint64_t device=0,vs=0,ps=0;unsigned ready=0;} xt_notice_log;
void (*xt_notice_callback)()=nullptr;
void log(const char*format,std::uint64_t device,std::uint64_t vs,std::uint64_t ps,unsigned a,unsigned b,unsigned c,unsigned d){
 if(std::strncmp(format,"linear_material_xt_default_unavailable ",sizeof("linear_material_xt_default_unavailable ")-1))return;
 ++xt_notice_log.calls;xt_notice_log.device=device;xt_notice_log.vs=vs;xt_notice_log.ps=ps;xt_notice_log.ready=a|(b<<1)|(c<<2)|(d<<3);
 if(xt_notice_callback)xt_notice_callback();
}
template<class...T>void log(const char*,T...){}
namespace x3::temporal {enum class AgxDecode{gamma22,srgb,none};}
namespace renderer {
struct CameraState{};
// Scene-boundary phase and sun-share frame doubles. The real boundary state
// machine and SunShareFrame accounting have their own tests; here only the
// fields and outcomes the extracted functions read are reproduced.
enum class BoundaryState {AwaitInitialClear,Background,Scene,AwaitCopy,AwaitBloomTarget,AwaitBloomDraw,AwaitDepthRebind,AwaitFinalClear,Selected,Rejected};
constexpr unsigned sun_untracked_reason_count=16;
enum class SunUntrackedReason : std::uint8_t {Unknown=0,Feature=1,Scene=2,Unregistered=3,Pair=4,NoZWrite=5,Blended=6,State=7,Rows=8,Geometry=9,NoDepth=10,FadeArm=11,ApplyFailed=12,Scope=13,History=14,ReadFailed=15};
struct SunShareFrame {
 std::uint32_t receivers=0,covered=0,untracked=0;std::uint32_t reasons[sun_untracked_reason_count]{};
 bool failed=false,published=false,available=false,coverage_required=false;
 unsigned draws=0,publishes=0;
 bool draw(bool success,bool receiver,bool conservative_coverage,bool depth_updated=false,SunUntrackedReason reason=SunUntrackedReason::Unknown)noexcept{
  ++draws;if(!success)return false;published=false;available=false;
  if(receiver)++receivers;else if(conservative_coverage){++covered;coverage_required=true;}
  else if(!depth_updated&&receivers){++untracked;failed=true;const unsigned i=unsigned(reason);++reasons[i<sun_untracked_reason_count?i:0u];return true;}
  return false;}
 bool publish(bool ready,bool owner_valid,bool coverage_valid)noexcept{
  ++publishes;published=true;available=ready&&owner_valid&&!failed&&(!coverage_required||coverage_valid);return available;}
 void reset()noexcept{*this=SunShareFrame{};}
};
struct LinearEmissionConfig {float gain=1;bool coverage=false;};
enum class LinearEmissionResult {Applied,UnsupportedShader,AllocationFailure};
bool linear_emission_config_valid(const LinearEmissionConfig& c){return std::isfinite(c.gain)&&c.gain>=0&&c.gain<=16;}
unsigned emission_transforms=0,emission_lookups=0;float emission_gain=0;bool emission_reject=false,emission_throw=false;
constexpr unsigned linear_emission_pair_count=20;
// Synthetic registry: (50,60) pair 0, (50,61) pair 1, (52,60) pair 2 (the shared PS 60 serves two pairs, as 8360f422 does).
unsigned linear_emission_pair_index(std::uint64_t vs,std::uint64_t ps){++emission_lookups;if(vs==50&&ps==60)return 0;if(vs==50&&ps==61)return 1;if(vs==52&&ps==60)return 2;return linear_emission_pair_count;}
bool linear_emission_pair_reviewed(std::uint64_t vs,std::uint64_t ps){return linear_emission_pair_index(vs,ps)<linear_emission_pair_count;}
LinearEmissionResult linear_emission_pixel_variant(const std::uint32_t*p,std::size_t,const LinearEmissionConfig& c,std::vector<std::uint32_t>& words){++emission_transforms;CHECK(c.coverage);emission_gain=c.gain;if(emission_throw)throw std::bad_alloc();if(emission_reject)return LinearEmissionResult::AllocationFailure;if(*p!=60&&*p!=61)return LinearEmissionResult::UnsupportedShader;words={*p+300};return LinearEmissionResult::Applied;}
// Step C promotion double: outcomes and the requested output set only; the
// real PackedScreen transform is checked by the pure SM1 transformer fixture.
enum class LinearEmissionSm1Outputs {Native=1,Emission=2,Coverage=3,PackedScreen=4,AdditiveGain=5};
struct LinearEmissionSm1Config {float gain=1;LinearEmissionSm1Outputs outputs=LinearEmissionSm1Outputs::Coverage;bool native_partial_precision=false;};
unsigned sm1_transforms=0;float sm1_gain=0;bool sm1_reject=false,sm1_additive=false;
LinearEmissionResult linear_emission_sm1_pixel_variant(const std::uint32_t*p,std::size_t,const LinearEmissionSm1Config&c,std::vector<std::uint32_t>&words){++sm1_transforms;CHECK(c.outputs==LinearEmissionSm1Outputs::PackedScreen||c.outputs==LinearEmissionSm1Outputs::AdditiveGain);sm1_additive=c.outputs==LinearEmissionSm1Outputs::AdditiveGain;sm1_gain=c.gain;if(sm1_reject)return LinearEmissionResult::AllocationFailure;if(*p!=95&&*p!=96)return LinearEmissionResult::UnsupportedShader;words={*p+700};return LinearEmissionResult::Applied;}
unsigned source_gain_transforms=0;float source_gain_value=0;bool source_gain_reject=false;
bool linear_emission_source_gain_valid(float g){return std::isfinite(g)&&g>=1&&g<=8;}
LinearEmissionResult linear_emission_source_gain_variant(const std::uint32_t*p,std::size_t,float g,std::vector<std::uint32_t>&words){++source_gain_transforms;source_gain_value=g;if(source_gain_reject)return LinearEmissionResult::AllocationFailure;if(*p!=60&&*p!=61)return LinearEmissionResult::UnsupportedShader;words={*p+900};return LinearEmissionResult::Applied;}
// Hull-emitter gain double (emitter plan phase 3): synthetic PS 62 is the one
// covered hull program; the real transformer is checked by its own oracle.
constexpr unsigned linear_emission_hull_program_count=12;
unsigned hull_gain_transforms=0;float hull_gain_value=0;bool hull_gain_reject=false;
unsigned linear_emission_hull_program_index(std::uint64_t ps){return ps==62?0u:linear_emission_hull_program_count;}
bool linear_emission_hull_program_reviewed(std::uint64_t ps){return linear_emission_hull_program_index(ps)<linear_emission_hull_program_count;}
LinearEmissionResult linear_emission_hull_source_gain_variant(const std::uint32_t*p,std::size_t,float g,std::vector<std::uint32_t>&words){++hull_gain_transforms;hull_gain_value=g;if(hull_gain_reject)return LinearEmissionResult::AllocationFailure;if(*p!=62)return LinearEmissionResult::UnsupportedShader;words={*p+900};return LinearEmissionResult::Applied;}
struct LinearMaterialConfig {float direct_gain=1,material_emissive_gain=1,lightmap_emissive_gain=1,fill=0;};
enum class LinearMaterialResult{Applied,UnsupportedShader};
unsigned fade_transforms=0,fade_lookups=0;bool fade_reject=false,fade_throw=false;
std::uint32_t linear_distance_fade_sampler_mask(std::uint64_t vs,std::uint64_t ps){++fade_lookups;return vs>=70&&vs<76&&ps==80+(vs-70)%4?15:0;}
bool linear_distance_fade_pair(std::uint64_t vs,std::uint64_t ps){return vs>=70&&vs<76&&ps==80+(vs-70)%4;}
LinearMaterialResult fade_variant(const std::uint32_t*p,std::vector<std::uint32_t>&o,bool vertex){++fade_transforms;if(fade_throw)throw std::bad_alloc();if(fade_reject||*p<(vertex?70u:80u)||*p>=(vertex?76u:84u))return LinearMaterialResult::UnsupportedShader;o={*p+600};return LinearMaterialResult::Applied;}
LinearMaterialResult linear_distance_fade_vertex_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o){return fade_variant(p,o,true);}
LinearMaterialResult linear_distance_fade_pixel_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&c,std::vector<std::uint32_t>&o,bool*fill_applied=nullptr){
 if(fill_applied)*fill_applied=false;
 const auto result=fade_variant(p,o,false);
 if(fill_applied&&result==LinearMaterialResult::Applied&&c.fill>0)*fill_applied=true;
 return result;
}
enum class MaterialMotionResult{Applied,UnsupportedShader};
enum class HdrTonemap{Agx,Identity};
struct MaterialMotionAbi{static constexpr UINT previous_vertex_constant=252,pixel_coordinates_constant=216;};
struct MotionOutputProfile{bool light_loop_bound_required=false;unsigned light_loop_max_count=8,matrix_register=0;}; MotionOutputProfile row;
using DepthPrepassProfile=MotionOutputProfile;
const DepthPrepassProfile* depth_prepass_vertex_row(std::uint64_t,std::size_t,std::uint32_t){return nullptr;}
struct HdrConfig{HdrTonemap tonemap=HdrTonemap::Agx;x3::temporal::AgxDecode decode=x3::temporal::AgxDecode::gamma22;};
bool linear_material_config_valid(const LinearMaterialConfig&c){return std::isfinite(c.direct_gain)&&c.direct_gain>=0&&c.direct_gain<=16;}
unsigned contract_lookups=0;
struct LinearMaterialScalarTransport {std::uint8_t source_texcoord=0,source_component=0,destination_texcoord=0,destination_component=0;};
struct LinearMaterialPairContract {std::uint32_t sampler_mask=0;bool bump=false;std::array<LinearMaterialScalarTransport,2> scalar_transport{};std::uint8_t scalar_transport_count=0;};
LinearMaterialPairContract linear_material_pair_contract(std::uint64_t vs,std::uint64_t ps){
 ++contract_lookups;
 if(vs==10&&ps>=22&&ps<=25)return {29,false};
 if(vs==30&&ps==44)return {57,true};
 if(vs==30&&ps==42)return {31,true,{{{6,0,1,3},{6,1,2,3}}},2};
 if(vs==30&&ps==43)return {31,true,{{{7,0,1,3},{}}},1};
 return vs==10&&ps==20?LinearMaterialPairContract{15,false}:vs==30&&ps==40?LinearMaterialPairContract{31,true}:vs==30&&ps==41?LinearMaterialPairContract{15,true}:LinearMaterialPairContract{};
}
unsigned xt_transforms=0,xt_lookups=0;
bool linear_material_xt_default_pair(std::uint64_t vs,std::uint64_t ps){++xt_lookups;return vs==10&&ps>=22&&ps<=25;}
unsigned asteroid_lookups=0;
bool linear_material_asteroid_pair(std::uint64_t vs,std::uint64_t ps){++asteroid_lookups;return vs>=70&&vs<76&&ps==80+(vs-70)%4;}
LinearMaterialResult linear_material_xt_default_vertex_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o,bool,bool linear){
 ++xt_transforms;if(*p!=10)return LinearMaterialResult::UnsupportedShader;o={*p+(linear?500u:400u)};return LinearMaterialResult::Applied;
}
LinearMaterialResult linear_material_xt_default_pixel_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o,bool,bool linear){
 ++xt_transforms;if(*p<22||*p>25)return LinearMaterialResult::UnsupportedShader;o={*p+(linear?500u:400u)};return LinearMaterialResult::Applied;
}
bool reject_row=false,throw_transform=false,reject_transform=false;
const MotionOutputProfile* material_motion_vertex_row(std::uint64_t hash,std::size_t){return reject_row||(hash>=50&&!(hash>=70&&hash<76))?nullptr:&row;}
const MotionOutputProfile* material_motion_pixel_row(std::uint64_t hash,std::size_t){return reject_row||(hash>=50&&!(hash>=70&&hash<76))?nullptr:&row;}
bool material_motion_vertex_exports_depth(const MotionOutputProfile&,bool x){return x;}
bool material_motion_pixel_writes_depth(const MotionOutputProfile&,bool x){return x;}
unsigned motion_transforms=0, material_transforms=0;
MaterialMotionResult material_motion_vertex_variant(const std::uint32_t*p,std::size_t,std::vector<std::uint32_t>&o,bool){++motion_transforms;if(throw_transform)throw std::bad_alloc();if(reject_transform)return MaterialMotionResult::UnsupportedShader;o={p[0]+100};return MaterialMotionResult::Applied;}
MaterialMotionResult material_motion_pixel_variant(const std::uint32_t*p,std::size_t n,std::vector<std::uint32_t>&o,bool d){return material_motion_vertex_variant(p,n,o,d);}
// Sun-share lane doubles (X3M_SUN_SHADOW_LANE): the extraction transform and
// the in-place invalidation of the shared RT2 write. Outcomes only; the real
// bytecode transforms are covered by the pure renderer fixtures.
unsigned sun_share_transforms=0,sun_share_invalidations=0;bool sun_share_reject=false,sun_share_invalid_reject=false,sun_share_extracts=true;
LinearMaterialResult linear_material_pixel_variant_sun_share(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o,bool,bool&extraction_applied){
 ++sun_share_transforms;extraction_applied=false;
 if(sun_share_reject)return LinearMaterialResult::UnsupportedShader;
 extraction_applied=sun_share_extracts;o={*p+800};return LinearMaterialResult::Applied;}
bool material_motion_invalid_sun_share(std::vector<std::uint32_t>&program){++sun_share_invalidations;if(sun_share_invalid_reject||program.empty())return false;program.front()+=1000;return true;}
LinearMaterialResult linear_material_vertex_variant(const std::uint32_t*p,std::size_t,const LinearMaterialConfig&,std::vector<std::uint32_t>&o,bool){++material_transforms;if(p[0]>=70)return LinearMaterialResult::UnsupportedShader;CHECK(p[0]==10||p[0]==20||p[0]==30||p[0]==40||p[0]==41||p[0]==42||p[0]==43||p[0]==21||(p[0]>=22&&p[0]<=25)||p[0]==44);o={p[0]+200};return LinearMaterialResult::Applied;}
LinearMaterialResult linear_material_pixel_variant(const std::uint32_t*p,std::size_t n,const LinearMaterialConfig&c,std::vector<std::uint32_t>&o,bool d){return linear_material_vertex_variant(p,n,c,o,d);}
LinearMaterialResult linear_material_pixel_variant_fill(const std::uint32_t*p,std::size_t n,const LinearMaterialConfig&c,std::vector<std::uint32_t>&o,bool d,bool&fill){fill=c.fill>0.f;return linear_material_pixel_variant(p,n,c,o,d);}
// Original fill (X3M_ORIGINAL_FILL): the reviewed-pair predicate and the
// create-time fill transform. fill_applied=0 is the fail-closed refusal of a
// program without a unique lobe sum; the real transform has its own fixture.
unsigned reviewed_lookups=0,original_fill_transforms=0;bool original_fill_reject=false,original_fill_applies=true;
bool linear_material_pair_reviewed(std::uint64_t vs,std::uint64_t ps){++reviewed_lookups;return linear_material_pair_contract(vs,ps).sampler_mask!=0;}
LinearMaterialResult linear_material_original_fill_pixel_variant(const std::uint32_t*p,std::size_t,float fill,std::vector<std::uint32_t>&o,bool,bool&fill_applied){
 ++original_fill_transforms;fill_applied=false;
 if(original_fill_reject)return LinearMaterialResult::UnsupportedShader;
 fill_applied=original_fill_applies&&fill>0.f;o={*p+900};return LinearMaterialResult::Applied;}
// Original-shading share producer (legacy-sun-application.md 1): same shape as
// the fill transform. share_applied=0 is the fail-closed refusal; the real
// transform has its own fixture.
unsigned original_share_transforms=0;bool original_share_reject=false,original_share_applies=true;
LinearMaterialResult linear_material_original_sun_share_pixel_variant(const std::uint32_t*p,std::size_t,float,std::vector<std::uint32_t>&o,bool,bool&share_applied,float=1.f,bool*gain_applied=nullptr,bool=false){
 ++original_share_transforms;share_applied=false;if(gain_applied)*gain_applied=false;
 if(original_share_reject)return LinearMaterialResult::UnsupportedShader;
 share_applied=original_share_applies;o={*p+950};return LinearMaterialResult::Applied;}
// Hull light-map gain double (--hull-lightmap-gain): inert unless a test arms it.
unsigned hull_lightmap_transforms=0;bool hull_lightmap_reject=false,hull_lightmap_applies=true;
LinearMaterialResult linear_material_hull_lightmap_gain_pixel_variant(const std::uint32_t*p,std::size_t,float,float,std::vector<std::uint32_t>&o,bool,bool&fill_applied,bool&gain_applied,bool=false){
 ++hull_lightmap_transforms;fill_applied=gain_applied=false;
 if(hull_lightmap_reject)return LinearMaterialResult::UnsupportedShader;
 gain_applied=hull_lightmap_applies;o={*p+960};return LinearMaterialResult::Applied;}
}
namespace renderer {
enum class LinearCompositionPolicy:unsigned{AdditiveEmission=1,DistanceFade=2,DistanceFadeInPlace=4,PackedScreenInPlace=8};
constexpr unsigned composition_policy_bit(LinearCompositionPolicy p){return unsigned(p);}
enum class LinearEmissionImage {None,Linear,Native,Incomplete};
struct LinearEmissionPreparation{bool ready=true,state_preserved=true;HRESULT saved=S_OK,operation=S_OK,restore=S_OK;};
struct LinearEmissionCompletion{LinearEmissionImage image=LinearEmissionImage::Linear;HRESULT source=S_OK,composition=S_OK,restore=S_OK;bool candidate_bound=true;HRESULT recovery=S_FALSE;RECT region{};};
struct LinearEmissionBoundary{IDirect3DSurface9*scene;IDirect3DPixelShader9*augmented;std::uint64_t frame;bool admitted;IDirect3DVertexShader9*augmented_vertex=nullptr;LinearCompositionPolicy policy=LinearCompositionPolicy::AdditiveEmission;RECT region{};bool region_known=false;};
// This double exposes transaction outcomes only; it never draws or simulates shader math.
struct LinearEmissionPass {
 struct Caps{bool enabled=true;unsigned supported_policies=1,available_policies=1;bool supports(LinearCompositionPolicy p)const{return (available_policies&unsigned(p))!=0;}const char*reason="scripted";}cap;D3DFORMAT attached_adapter_format=D3DFMT_UNKNOWN;unsigned requested_policies=0,clears=0;std::uint64_t last_frame=0;std::array<unsigned char,4>mask_bytes{0,0,0,0};unsigned attaches=0,prepares=0,finishes=0,recoveries=0,acks=0,begins=0,ensures=0,held=7;
 bool busy=false,coverage=true,candidate_available=true;HRESULT attach_result=S_OK,ensure_result=S_OK,ack_result=S_OK,last_source=S_OK;
 LinearEmissionPreparation preparation{},begin{};LinearEmissionCompletion completion{},recovery{LinearEmissionImage::Native,S_OK,S_OK,S_OK,true};
 IDirect3DSurface9 candidate_storage,*candidate=&candidate_storage;LinearEmissionBoundary boundary{};std::vector<bool> exchanges;
 template<class A,class B,class C>HRESULT attach(A,B,C,D3DFORMAT adapter,D3DFORMAT,unsigned policies){++attaches;attached_adapter_format=adapter;requested_policies=policies;return attach_result;}
 float gain=1;bool configure_packed_gain(float g){if(!(g>=0&&g<=16))return false;gain=g;return true;}float packed_gain()const{return gain;}
 const Caps&caps()const{return cap;}unsigned references()const{return held;}bool reference_accounting_busy()const{return busy;}
 HRESULT ensure_targets(UINT,UINT){++ensures;return ensure_result;}
 LinearEmissionPreparation begin_frame(std::uint64_t frame){++begins;if(frame!=last_frame){++clears;mask_bytes.fill(0);last_frame=frame;}coverage=begin.ready;return begin;}
 LinearEmissionPreparation prepare(const LinearEmissionBoundary&b){++prepares;boundary=b;return preparation;}
 LinearEmissionCompletion finish(HRESULT source){++finishes;last_source=source;completion.source=source;if(FAILED(source)||FAILED(completion.restore)||!completion.candidate_bound)coverage=false;candidate_available=candidate_available&&completion.candidate_bound&&SUCCEEDED(completion.restore);return completion;}
 IDirect3DSurface9**owning_candidate(){return candidate_available?&candidate:nullptr;}
 HRESULT acknowledge_exchange(bool exchanged){++acks;exchanges.push_back(exchanged);return ack_result;}
 LinearEmissionCompletion recover_native(){++recoveries;candidate_available=recovery.candidate_bound&&SUCCEEDED(recovery.restore);if(!candidate_available)coverage=false;recovery.source=last_source;return recovery;}
 bool coverage_valid()const{return coverage;}void before_reset(){held=0;coverage=false;}void detach(){held=0;}
};
}
struct Pass {IDirect3DSurface9 initial,*current=&initial;unsigned exchange_calls=0;std::vector<HRESULT> exchange_results;
 IDirect3DSurface9*target(){return current;}UINT width(){return 64;}UINT height(){return 32;}
 HRESULT exchange_target(IDirect3DSurface9*&candidate){const auto i=exchange_calls++;const HRESULT hr=i<exchange_results.size()?exchange_results[i]:S_OK;if(SUCCEEDED(hr))std::swap(candidate,current);return hr;}
 bool active=true;unsigned references(){return 0;} bool tonemap_active()const{return active;}void shutdown(){}void after_reset(HRESULT){}void before_reset(){}void bind(void*,void*){}void detach(){}};
struct History{renderer::BoundaryState boundary=renderer::BoundaryState::Scene;renderer::BoundaryState state()const noexcept{return boundary;}void invalidate(){}};
namespace camera_state {void reset(){} bool request_consumer(){return true;}}
// Step C admission double: synthetic screen identities (vs 90/91, ps 95/96)
// stand in for the nine SM1 pairs; the real table is a header constant checked
// by its own test, never re-encoded here.
namespace screen_emission {
unsigned pair_lookups=0,pixel_lookups=0,vertex_lookups=0;
inline bool admitted_vertex_shader(std::uint64_t vs)noexcept{++vertex_lookups;return vs==90;}
inline bool admitted_pair(std::uint64_t vs,std::uint64_t ps)noexcept{++pair_lookups;return (vs==90&&ps==95)||(vs==91&&ps==96);}
inline bool admitted_pixel_shader(std::uint64_t ps)noexcept{++pixel_lookups;return ps==95||ps==96;}
constexpr unsigned pair_count=9;
inline unsigned admitted_pair_index(std::uint64_t vs,std::uint64_t ps)noexcept{++pair_lookups;return vs==90&&ps==95?0u:vs==91&&ps==96?1u:pair_count;}
}
namespace cutout {enum class Capability:std::uint8_t{Pending,Ready,Unsupported,Retry};
unsigned pair_lookups=0;constexpr bool pair(std::uint64_t,std::uint64_t) noexcept {return false;}}
namespace fade_route {
struct Registers{std::uint8_t alpha=0,fog=0;};
bool registers(std::uint64_t vs,Registers&out){if(vs<70||vs>=76)return false;out={39,41};return true;}
constexpr unsigned threshold_off=1001;
struct Hysteresis{static constexpr unsigned band=100u;void clear()noexcept{}};
}
// Mirrors src/proxy/motion_output.h's TaaInvalidateSite; the extracted code
// names sites, and this double only counts the calls.
enum class TaaInvalidateSite:unsigned{RestoreFailed=0,StateLost=1,Skip=2,Target=3,Container=4,ResolveFailed=5,NotResolved=6,PresentFailed=7,Reset=8,ComparisonExposure=9,ComparisonStateFailed=10,CompositionStateLost=11,CompositionReaders=12,CompositionExport=13,CompositionAttach=14,CompositionBegin=15,CompositionRefused=16,CompositionPrepare=17,CompositionIncomplete=18,CutoutMissed=19,Count=20};
enum class MotionGate{Feature=1};
struct MotionDrawCall{bool indexed=true,user_memory=false;unsigned primitives=3;bool composition_permission=true;};
namespace telemetry{enum class Metric{RouteGate,RouteSetRenderTarget,RouteLazyFlush};bool draw_enabled(){return false;}}

// Inert mirror of the X3M_FRAME_TIMING redundant-state counters
// (src/proxy/frame_timing.h): the extracted shadow updates report into them
// and they measure nothing here.
namespace shadow_retention {enum class Flush:std::uint8_t{None,Epoch,Reset,Device,Teardown,Sun,Observer};}
namespace frame_timing {
enum class StateSet : unsigned { RenderState = 0, SamplerState = 1, Texture = 2 };
inline void state_write(StateSet, unsigned, bool, bool) noexcept {}
}
std::uint64_t draw_stamp(){return 0;}
struct MotionRoute {
 renderer::LinearCompositionPolicy composition_policy=renderer::LinearCompositionPolicy::AdditiveEmission;
 MotionGate gate=MotionGate::Feature;bool routed=false,composition=false,scene=true,submit=true,evaluated=false;HRESULT submission_error=D3DERR_INVALIDCALL,preparation_error=S_OK;std::uint64_t ticks=0;
 bool depth=false,linear_material=false,fade_arm=false,vs_set=false,ps_set=false,write2_set=false,rt2_set=false,write_set=false,rt_set=false;
 bool vs_constants_set=false,ps_constants_set=false,jittered=true,hull_lightmap=false;
 DWORD saved_write1=15,saved_write2=15;
 struct Region {RECT rect{};unsigned reason=0;bool bound=false;};
 Region fade_region{};bool fade_region_evaluated=false;unsigned fade_region_permille=0;
 Region prefix_region{};bool prefix_evaluated=false;unsigned prefix_region_permille=0;
 bool sun_color_writer=false,sun_receiver=false;std::uint8_t sun_z_state=0,sun_refusal=0;std::uint16_t sun_draw_state=0;bool native_mip_bias=false;
 bool original_fill=false; // the fill variant the bind path selected for this route
};
struct Counters{bool hook_scene_end=false,bloom_copy_seen=false;unsigned material_routed=0,material_bump_routed=0;unsigned set_rt=0,set_rt_ticks=0,lazy_flushes=0,lazy_mask_writes=0;unsigned gates[8]{},fill_ticks=0,lazy_flush_ticks=0,gate_ticks=0,mip_bias_restores=0,mip_bias_failures=0;unsigned draws=0,restore_failures=0,material_bind_failures=0,mip_bias_game_writes=0,rs_resyncs=0,sb_resyncs=0;};
struct D3DDISPLAYMODE{D3DFORMAT Format=D3DFMT_UNKNOWN;};
struct Device {
 unsigned display_mode_reads=0;HRESULT display_mode_result=S_OK;D3DFORMAT display_mode_format=1;
 unsigned stage_reads=0;DWORD stage_flags=0;HRESULT stage_result=S_OK;
 HRESULT target_result=S_OK;std::vector<int> calls; std::vector<unsigned> failed_calls; unsigned ordinal=0;
 IDirect3DVertexShader9* bound_vs=nullptr;IDirect3DPixelShader9* bound_ps=nullptr;
 std::array<DWORD,6> srgb{};unsigned sampler_reads=0;int fail_sampler=-1;bool fail_combined_create=false,fail_motion_create=false,fail_get_vs=false,fail_get_ps=false;
 std::map<DWORD,IDirect3DBaseTexture9*>textures;std::map<DWORD,HRESULT>texture_failures;unsigned texture_reads=0;
 unsigned emission_creates=0,vs_creates=0;bool fail_emission_create=false,partial_emission_create=false,null_emission_create=false;
 bool mutation_faults=false;unsigned state_ordinal=0;std::vector<unsigned>state_failures;
 std::array<IDirect3DSurface9*,3>targets{};std::array<DWORD,3>write_masks{15,5,6};std::array<float,16>vs_constants{};std::array<float,8>ps_constants{};
 HRESULT state_result(){if(!mutation_faults)return S_OK;++state_ordinal;for(auto n:state_failures)if(n==state_ordinal)return E_FAIL;return S_OK;}
 DWORD fail_program=0;bool partial_program=false;
 bool fails(){++ordinal;for(auto index:failed_calls)if(ordinal==index)return true;return false;}
};
struct IDirect3DVertexBuffer9:IUnknown{};struct IDirect3DIndexBuffer9:IUnknown{};struct IDirect3DVertexDeclaration9:IUnknown{};
struct D3DVIEWPORT9{DWORD X=0,Y=0,Width=1,Height=1;float MinZ=0,MaxZ=1;};
struct Surface{bool known=false;};
struct Viewport{bool known=false;DWORD x=0,y=0,w=0,h=0;float low=0,high=1;};
Surface describe_surface(IDirect3DSurface9*surface){return {surface&&surface->describable};}
bool same(const Surface&,const Surface&){return false;}
constexpr struct {unsigned count=1;unsigned base[1]={24};}matrix_windows;
template<class T>struct ThrowingMap:std::map<void*,T>{bool fail_next=false;T&operator[](void*key){if(fail_next){fail_next=false;throw std::bad_alloc();}return std::map<void*,T>::operator[](key);}};
using D=Device*;
using GetDisplayModeFn=HRESULT(*)(D,UINT,D3DDISPLAYMODE*);
using SetRenderTargetFn=HRESULT(*)(D,DWORD,IDirect3DSurface9*);
using SetVsFn=HRESULT(*)(D,IDirect3DVertexShader9*);using SetPsFn=HRESULT(*)(D,IDirect3DPixelShader9*);
using CreateVsFn=HRESULT(*)(D,const DWORD*,IDirect3DVertexShader9**);using CreatePsFn=HRESULT(*)(D,const DWORD*,IDirect3DPixelShader9**);
using SetSamplerStateFn=HRESULT(*)(D,DWORD,D3DSAMPLERSTATETYPE,DWORD);
using GetRenderStateFn=HRESULT(*)(D,DWORD,DWORD*);
using GetStageFn=HRESULT(*)(D,DWORD,unsigned,DWORD*);
using GetSamplerStateFn=HRESULT(*)(D,DWORD,D3DSAMPLERSTATETYPE,DWORD*);
using GetTextureFn=HRESULT(*)(D,DWORD,IDirect3DBaseTexture9**);
using SetRenderStateFn=HRESULT(*)(D,DWORD,DWORD);using SetConstantsFFn=HRESULT(*)(D,UINT,const float*,UINT);
using GetVsFn=HRESULT(*)(D,IDirect3DVertexShader9**);using GetPsFn=HRESULT(*)(D,IDirect3DPixelShader9**);
using GetConstantsFFn=HRESULT(*)(D,UINT,float*,UINT);using GetConstantsIFn=HRESULT(*)(D,UINT,int*,UINT);
using GetStreamFn=HRESULT(*)(D,UINT,IDirect3DVertexBuffer9**,UINT*,UINT*);using GetIndicesFn=HRESULT(*)(D,IDirect3DIndexBuffer9**);
using GetDeclarationFn=HRESULT(*)(D,IDirect3DVertexDeclaration9**);using GetRenderTargetFn=HRESULT(*)(D,DWORD,IDirect3DSurface9**);
using GetDepthFn=HRESULT(*)(D,IDirect3DSurface9**);using GetViewportFn=HRESULT(*)(D,D3DVIEWPORT9*);
enum Slots{SetRenderTarget,SetSamplerState,SetVertexShader,SetPixelShader,CreateVertexShader,CreatePixelShader,GetSamplerState,GetTexture,SetRenderState,SetVertexShaderConstantF,SetPixelShaderConstantF,GetVertexShader,GetPixelShader,GetVertexShaderConstantF,GetVertexShaderConstantI,GetPixelShaderConstantF,GetStreamSource,GetIndices,GetVertexDeclaration,GetRenderTarget,GetDepthStencilSurface,GetViewport,GetRenderState,GetDisplayMode,GetTextureStageState};
HRESULT set_vs(D d,IDirect3DVertexShader9*p){d->calls.push_back(1);d->bound_vs=p;if(d->fails())return E_FAIL;return S_OK;}
HRESULT set_ps(D d,IDirect3DPixelShader9*p){d->calls.push_back(2);d->bound_ps=p;if(d->fails())return E_FAIL;return S_OK;}
HRESULT create_vs(D d,const DWORD*p,IDirect3DVertexShader9**out){++d->vs_creates;if(*p==d->fail_program){if(d->partial_program)*out=new IDirect3DVertexShader9;return E_FAIL;}if((*p>=200&&d->fail_combined_create)||(*p<200&&d->fail_motion_create))return E_FAIL;*out=new IDirect3DVertexShader9;return S_OK;}
HRESULT create_ps(D d,const DWORD*p,IDirect3DPixelShader9**out){if(*p==d->fail_program){if(d->partial_program)*out=new IDirect3DPixelShader9;return E_FAIL;}if(*p>=300&&*p<400){++d->emission_creates;if(d->partial_emission_create){*out=new IDirect3DPixelShader9;return E_FAIL;}if(d->fail_emission_create)return E_FAIL;if(!d->null_emission_create)*out=new IDirect3DPixelShader9;return S_OK;}if((*p>=200&&d->fail_combined_create)||(*p<200&&d->fail_motion_create))return E_FAIL;*out=new IDirect3DPixelShader9;return S_OK;}
HRESULT get_sampler(D d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD*out){CHECK(type==D3DSAMP_SRGBTEXTURE);++d->sampler_reads;if(int(stage)==d->fail_sampler)return E_FAIL;*out=d->srgb[stage];return S_OK;}
HRESULT get_texture(D d,DWORD stage,IDirect3DBaseTexture9**out){++d->texture_reads;*out=d->textures[stage];if(*out)(*out)->AddRef();auto it=d->texture_failures.find(stage);return it==d->texture_failures.end()?S_OK:it->second;}
HRESULT set_target(D d,DWORD index,IDirect3DSurface9*p){d->targets[index]=p;const auto hr=d->state_result();return FAILED(hr)?hr:d->target_result;}
HRESULT set_state(D d,DWORD index,DWORD value){d->write_masks[index==D3DRS_COLORWRITEENABLE1?1:2]=value;return d->state_result();}
HRESULT set_sampler(D d,DWORD,D3DSAMPLERSTATETYPE,DWORD){return d->fails()?E_FAIL:S_OK;}
HRESULT set_constants(D d,UINT start,const float*p,UINT count){if(start==252)std::memcpy(d->vs_constants.data(),p,count*4*sizeof(float));else std::memcpy(d->ps_constants.data(),p,count*4*sizeof(float));return d->state_result();}
HRESULT get_vs(D d,IDirect3DVertexShader9**p){if(d->fail_get_vs)return E_FAIL;*p=d->bound_vs;if(*p)(*p)->AddRef();return S_OK;}
HRESULT get_ps(D d,IDirect3DPixelShader9**p){if(d->fail_get_ps)return E_FAIL;*p=d->bound_ps;if(*p)(*p)->AddRef();return S_OK;}
HRESULT get_f(D,UINT,float*,UINT){return S_OK;}HRESULT get_i(D,UINT,int*,UINT){return S_OK;}
HRESULT get_stream(D,UINT,IDirect3DVertexBuffer9**p,UINT*,UINT*){*p=nullptr;return S_OK;}
HRESULT get_indices(D,IDirect3DIndexBuffer9**p){*p=nullptr;return S_OK;}
HRESULT get_declaration(D,IDirect3DVertexDeclaration9**p){*p=nullptr;return S_OK;}
HRESULT get_target(D,DWORD,IDirect3DSurface9**p){*p=nullptr;return D3DERR_NOTFOUND;}
HRESULT get_depth(D,IDirect3DSurface9**p){*p=nullptr;return D3DERR_NOTFOUND;}
HRESULT get_display_mode(D d,UINT index,D3DDISPLAYMODE*out){CHECK(index==0);++d->display_mode_reads;out->Format=d->display_mode_format;return d->display_mode_result;}
HRESULT get_state(D,DWORD,DWORD*out){*out=0;return S_OK;}
HRESULT get_stage(D d,DWORD stage,unsigned type,DWORD*out){CHECK(stage==0&&type==D3DTSS_TEXTURETRANSFORMFLAGS);++d->stage_reads;*out=d->stage_flags;return d->stage_result;}
HRESULT get_viewport(D,D3DVIEWPORT9*){return S_OK;}
// Defined by the extracted production source included below.
constexpr unsigned shadow_index(D3DRENDERSTATETYPE state) noexcept;
class MotionOutput {
public:
 struct ShaderEntry {std::uint64_t hash=0;IUnknown*variant=nullptr,*material_variant=nullptr,*xt_default_ordinary_variant=nullptr,*distance_fade_variant=nullptr;IDirect3DVertexShader9*xt_default_linear_variant=nullptr;IDirect3DPixelShader9*original_fill_variant=nullptr;IDirect3DPixelShader9*emission_variant=nullptr,*source_gain_variant=nullptr,*hull_gain_variant=nullptr,*screen_variant=nullptr,*screen_additive_variant=nullptr,*sun_original_variant=nullptr,*sun_original_lightmap_variant=nullptr,*hull_lightmap_variant=nullptr;bool hull_program=false;IDirect3DPixelShader9*sun_motion_variant=nullptr,*sun_material_variant=nullptr,*sun_xt_variant=nullptr;bool sun_extraction=false;bool registered=false;const renderer::MotionOutputProfile*row=nullptr,*prepass=nullptr;std::int8_t sun_register=-1;};
 struct Shadow {
 IDirect3DVertexShader9*vs=nullptr,*vs_variant=nullptr,*vs_material_variant=nullptr;
 IDirect3DPixelShader9*ps=nullptr,*ps_variant=nullptr,*ps_material_variant=nullptr;
 bool emission_pair=false;std::uint32_t fade_sampler_mask=0;IDirect3DVertexShader9*vs_fade_variant=nullptr;IDirect3DPixelShader9*ps_fade_variant=nullptr;
 bool vs_registered=false,ps_registered=false;std::int8_t ps_sun_register=-1;IDirect3DPixelShader9*ps_emission_variant=nullptr,*emission_eligible_variant=nullptr;
 IDirect3DPixelShader9*ps_sun_motion=nullptr,*ps_sun_material=nullptr,*ps_sun_xt=nullptr;bool ps_sun_extraction=false;
 IDirect3DPixelShader9*ps_sun_original=nullptr;bool original_share_pair=false,original_share_refused=false; // original share variant (legacy-sun-application.md 4.1)
 IDirect3DPixelShader9*ps_sun_original_lightmap=nullptr,*ps_hull_lightmap_variant=nullptr;bool hull_lightmap_pair=false; // hull light-map gain variants (hull-self-illumination.md 5)
 IDirect3DPixelShader9*ps_source_gain_variant=nullptr,*source_gain_eligible_variant=nullptr;unsigned source_gain_pair=renderer::linear_emission_pair_count;
 bool ps_hull_program=false;IDirect3DPixelShader9*ps_hull_gain_variant=nullptr;
 bool screen_additive_pair=false;unsigned screen_additive_index=screen_emission::pair_count;IDirect3DPixelShader9*ps_screen_additive_variant=nullptr;
 bool screen_pair=false;IDirect3DPixelShader9*ps_screen_variant=nullptr,*screen_eligible_variant=nullptr;std::uint64_t stream0=0;
 std::uint64_t vs_hash=0,ps_hash=0;const renderer::MotionOutputProfile*vs_row=nullptr,*vs_prepass=nullptr;
 IDirect3DPixelShader9*ps_original_fill_variant=nullptr;bool original_fill_pair=false;
 bool xt_default_pair=false,xt_default_ready=false,cutout_pair=false,asteroid_pair=false,fade_route_pair=false;
 fade_route::Registers fade_route_registers{};
 DWORD fill_mode=0;bool fill_mode_known=false;
 IDirect3DVertexShader9*vs_xt_default_ordinary=nullptr,*vs_xt_default_linear=nullptr;IDirect3DPixelShader9*ps_xt_default_ordinary=nullptr;
 DWORD composition_blend[composition_blend_count]{};bool composition_blend_known[composition_blend_count]{};
 DWORD states[motion_shadow_state_count]{};bool states_known[motion_shadow_state_count]{};
 renderer::LinearMaterialPairContract material_contract{};float rows[1][16]{};bool rows_known[1]{};int integer0[4]{};bool integer0_known=false;
 Surface rt0,depth;Viewport viewport;bool extra_rt[4]{};
 bool recording=false,vs_reserved_written=false,ps_reserved_written=false;float vs_reserved[16]{},ps_reserved[8]{};
 }shadow_;
 struct SamplerShadow {IDirect3DBaseTexture9*texture=nullptr;DWORD levels=0,mipfilter=0,saved_bias=0,srgb=0;bool srgb_known=false,mipfilter_known=false,saved_known=false,biased=false;};
 static constexpr unsigned sampler_stage_count=16,failure_log_limit=8;
 SamplerShadow samplers_[16];
 ThrowingMap<ShaderEntry>vertex_,pixel_;
 bool state_shadow_=false,state_hooks_=true; // hooks on: the extracted refusal reads the shadow's flag (hybrid unhook mirror)
 bool sampler_srgb_known(unsigned stage)noexcept{return stage<sampler_stage_count&&samplers_[stage].srgb_known;}
 bool state_known(unsigned i)noexcept{return i<motion_shadow_state_count&&shadow_.states_known[i];}
 bool blend_known(unsigned i)noexcept{return i<composition_blend_count&&shadow_.composition_blend_known[i];}
 bool fill_mode_known()noexcept{return shadow_.fill_mode_known;}
 void begin_draw_reads()noexcept{} // hooks on: never reached
 long state_field(unsigned i)noexcept{return state_known(i)?long(shadow_.states[i]):-1;}
 D device_=nullptr;bool enabled_=true,requested_=true,depth_enabled_=true,linear_material_requested_=false;
 renderer::LinearMaterialConfig linear_material_config_{};
 struct XtDefaultUnavailable {std::uint64_t device=0,vs=0,ps=0;unsigned ready_mask=0;bool seen=false,pending=false;} xt_default_unavailable_;
 std::unique_ptr<renderer::LinearEmissionPass>composition_;
 bool motion_state_lost_=false;HRESULT motion_state_error_=D3DERR_INVALIDCALL;
 void recover_motion_state()noexcept; HRESULT restore_wrap_states(MotionRoute&){return S_OK;}
 bool composition_busy_=false,composition_state_lost_=false,composition_frame_stopped_=false,composition_enhanced_=false,composition_quarantined_=false,composition_readers_known_=false,composition_published_=false;
 IDirect3DTexture9*composition_main_texture_=nullptr;IUnknown*composition_main_identity_=nullptr;
 std::uint32_t composition_main_sampler_mask_=0,composition_reader_known_mask_=0;IDirect3DBaseTexture9*composition_textures_[21]{};
 struct{unsigned eligible_fade=0,prepared_fade=0,linear_fade=0;std::uint64_t pool_traffic_bytes=0;unsigned refused=0,prepared=0,suppressed=0,incomplete=0,linear=0,native=0,exports=0,exchanged=0;HRESULT source=S_OK,prepare=S_OK,prepare_restore=S_OK,composition=S_OK,restore=S_OK,exchange=S_OK,ack=S_OK;unsigned refusal[6]{},prepare_failures=0,composition_failures=0,restore_failures=0,exchange_failures=0,ack_failures=0;unsigned in_place=0,in_place_linear=0,in_place_incomplete=0,recovery_failures=0;std::uint64_t region_pixels=0;unsigned packed_eligible=0,packed_unbounded_refused=0,packed_caps_refused=0,packed_admitted=0,packed_linear=0,packed_incomplete=0;std::uint64_t packed_region_pixels=0;HRESULT recovery=S_FALSE;}composition_counts_;
 unsigned composition_adapter_format_=1,composition_depth_format_=2;bool composition_attach_attempted_=true,composition_effective_=false,composition_identity_known_=true;void*native_=nullptr;struct{struct{unsigned format=2;}depth;}pending_;
 bool taa_enabled_=true,hdr_dirty_=false,bound_scene=true;IUnknown*hdr_resolved_=nullptr;unsigned active_queries_=0,taa_invalidations=0;std::uint32_t taa_invalidate_pending_=0;
 unsigned mip_bias_logged_game_writes_=0;bool lazy_rt1_=false,lazy_rt2_=false,lazy_mode_=false;
 struct FadeBounds{bool storage=false,fail_reserve=false;unsigned clears=0,reserves=0;
  bool reserve(){++reserves;if(fail_reserve)return false;storage=true;return true;}
  void clear(){++clears;storage=false;}bool reserved()const{return storage;}}fade_bounds_;
 struct FadeWitness{static constexpr unsigned rect_capacity=1024,buckets=8,line_budget=64;
  RECT rects[rect_capacity]{};bool prepared[rect_capacity]{};
  unsigned count=0,prepared_count=0,last=rect_capacity,logged=0;bool overflow=false;unsigned f_hist[buckets]{};
  IDirect3DSurface9*copy=nullptr;}fade_witness_;
 unsigned witness_releases_=0,fade_regions_derived_=0,cutout_candidates_=0,mip_bias_failure_reports_=0;
 bool witness_frame_=false;bool witness_frame()const noexcept{return witness_frame_;}
 void release_fade_witness()noexcept{++witness_releases_;}
 // Capture-only packed_sample readback surface and its sampling seam.
 struct{bool valid=false;unsigned sampled=0;IDirect3DSurface9*copy=nullptr,*pre_copy=nullptr;std::uint32_t copy_width=0,copy_height=0,copy_format=0;}packed_sample_;
 unsigned packed_sample_releases_=0,packed_samples_pre_=0,packed_samples_post_=0;
 void sample_packed_pre(const MotionRoute&)noexcept{++packed_samples_pre_;}
 void sample_packed_post(const RECT&)noexcept{++packed_samples_post_;}
 void release_packed_sample()noexcept{++packed_sample_releases_;packed_sample_.copy=nullptr;packed_sample_.pre_copy=nullptr;}
 void report_mip_bias_game_write_failure()noexcept{++mip_bias_failure_reports_;}
 void derive_fade_region(MotionRoute&route)noexcept{++fade_regions_derived_;route.fade_region_evaluated=true;}
 void mark_cutout_candidate(MotionRoute&)noexcept{++cutout_candidates_;}
 // Capture-only refused-rectangle record: a double for the extracted code,
 // unused while the production refusal path does not call it.
 bool capture_=false;unsigned fade_refusals_recorded_=0;
 void record_fade_refused(const MotionRoute&,unsigned)noexcept{++fade_refusals_recorded_;}
 std::uint32_t sampler_restore_failed_mask_=0;
 void release_mip_bias_retry_bound()noexcept{sampler_restore_failed_mask_=0;}
 cutout::Capability cutout_caps_=cutout::Capability::Pending;HRESULT cutout_cap_result_=S_FALSE;
 bool cutout_probe_frame_known_=false,cutout_reset_pending_=false,shimmer_trace_=false;
 unsigned cutout_probes_=0;void probe_cutout_caps(bool=false)noexcept{++cutout_probes_;}
 std::uint64_t last_routed_node_=0,last_routed_lifetime_=0,last_routed_frame_=~std::uint64_t{0};std::uint32_t last_routed_draw_=0; // overlay witness latch, cleared by before_reset
 bool distance_fade_requested_=false;unsigned fade_route_threshold_=500;fade_route::Hysteresis fade_hysteresis_;unsigned composition_required_producers_=0;HRESULT composition_attach_result_=S_FALSE;
 bool original_fill_requested_=false;float original_fill_=0.f;std::uint32_t original_fill_draws_=0;unsigned sun_original_refused_draws_=0;
 bool linear_emission_requested_=false;renderer::LinearEmissionConfig linear_emission_config_{1,true};
 bool screen_emission_requested_=false,screen_emission_bound_=false;float screen_emission_gain_=1;unsigned prefix_regions_derived_=0;
 bool emission_source_gain_requested_=false;float emission_source_gain_=1;
 bool hull_emission_gain_requested_=false;float hull_emission_gain_=1;
 bool lightmap_far_fade_=false; // --light-map-far-fade, mirrored inertly (off)
 bool hull_lightmap_gain_requested_=false;float hull_lightmap_gain_=1.f;std::uint32_t hull_lightmap_draws_=0,sun_original_lightmap_variants_=0;
 struct{std::uint32_t admitted=0,refused_blend=0,refused_variant=0,refused_routed=0,refused_unknown=0,refused_state=0,bind_failures=0,programs=0;}hull_gain_counts_;
 std::uint32_t hull_gain_logged_[3]{};std::uint32_t hull_gain_program_logged_=0;unsigned hull_gain_prepares_=0;
 void prepare_hull_gain(const MotionDrawCall&,MotionRoute&)noexcept{++hull_gain_prepares_;}
 bool screen_additive_requested_=false;float screen_additive_gain_=1;
 bool screen_additive_enabled_=true;bool source_gain_enabled_=true;bool hull_gain_enabled_=true;bool hull_lightmap_enabled_=true; // runtime hotkey flags (F5, F6 for the effects gain and the guide lights, F4 for the light-map gain); the fixture exercises the default-on path
 bool emission_source_gain_enabled()const noexcept{return source_gain_enabled_;}
 unsigned screen_additive_frame_admitted_=0,screen_additive_frame_refused_=0,screen_additive_frame_pairs_=0;
 bool sun_lane_requested_=false,sun_lane_qualified_=false,sun_lane_active_=false,sun_lane_failed_=false;
 renderer::SunShareFrame sun_frame_{};unsigned sun_writer_count_=0,sun_writer_overflow_=0;
 bool sun_coverage_current_=false,sun_composition_completed_=false;
 unsigned sun_qualifications_=0;void qualify_sun_lane()noexcept{++sun_qualifications_;sun_lane_qualified_=sun_lane_requested_;}
 std::uint32_t source_gain_logged_[4]{};std::uint32_t source_gain_pair_logged_=0;
 unsigned source_gain_prepares_=0,screen_additive_prepares_=0;
 void prepare_source_gain(const MotionDrawCall&,MotionRoute&)noexcept{++source_gain_prepares_;}
 void prepare_screen_additive(const MotionDrawCall&,MotionRoute&)noexcept{++screen_additive_prepares_;}
 long shadow_state_field(D3DRENDERSTATETYPE state)const noexcept{const unsigned i=shadow_index(state);return i<motion_shadow_state_count&&shadow_.states_known[i]?long(shadow_.states[i]):-1;}
 // Step B's bound derivation is a separate seam; here it only records the call.
 void derive_prefix_region(const MotionDrawCall&,MotionRoute&)noexcept{++prefix_regions_derived_;}
 bool releasing_=false,taa_busy_=false,hdr_enabled_=true,fill_pending_=false;
 bool hdr_target_failed_=false,hdr_blocked_=false,target_failed_=false,pending_valid_=false,main_msaa_=false,msaa_logged_=false;unsigned hdr_blocked_latches_=0,main_msaa_samples_=0;Surface main_,main_depth_;History selector_;renderer::CameraState camera_previous_;
 unsigned taa_references_=0;std::unique_ptr<Pass>hdr_=std::make_unique<Pass>(),taa_;History history_;
 // Ambient occlusion pass and its timestamp queries (step 2): lifetime seams only.
 std::unique_ptr<Pass>ao_;bool ao_timing_created_=false,ao_timing_failed_=false,ao_timing_lost_=false,ao_attach_failed_=false;unsigned ao_timing_releases_=0,ao_chain_failures_=0,ao_attach_count_=0;std::uint64_t ao_attach_frame_=0;D3DFORMAT ao_adapter_format_=D3DFMT_UNKNOWN,ao_target_format_=D3DFMT_UNKNOWN;
 void ao_timing_release()noexcept{++ao_timing_releases_;ao_timing_created_=false;}
 // Cascade-0 depth replay (default off): only the lifetime seams the extracted
 // control flow touches - lease release, detach, Reset and re-attach state.
 bool depth_replay_requested_=false,depth_replay_attach_failed_=false;std::unique_ptr<Pass>depth_replay_;unsigned depth_lease_releases_=0;
 void release_depth_leases()noexcept{++depth_lease_releases_;}
 // Scene-end sun-shadow apply quad (sun_shadow_apply_pass.h) and the candidate
 // extent queue: lifetime seams only, counted here.
 // Caster retention (default off): teardown and Reset flush seams only.
 bool retention_=false;unsigned retention_detaches_=0,retention_reset_flushes_=0;
 void detach_shadow_retention()noexcept{++retention_detaches_;}
 void flush_shadow_retention(shadow_retention::Flush reason)noexcept{retention_reset_flushes_+=reason==shadow_retention::Flush::Reset;}
 std::unique_ptr<Pass>sun_apply_;unsigned candidate_extent_releases_=0;
 void release_candidate_extents()noexcept{++candidate_extent_releases_;}
 bool candidates_requested_=false,sun_apply_applied_=false,sun_apply_attempted_=false,sun_apply_attach_failed_=false,depth_cascade_frame_ok_=false;
 std::uint32_t candidate_ps_written_=0;
 unsigned depth_replayed_=0,sun_original_refused_=0,sun_original_variants_=0;
 std::uint64_t sun_apply_frame_=~std::uint64_t(0),depth_replayed_frame_=~std::uint64_t(0);
 IUnknown*target_surface_=nullptr,*depth_surface_=nullptr,*sentinel_ps_=nullptr,*sentinel_mrt_ps_=nullptr,*quad_vs_=nullptr,*quad_declaration_=nullptr;
 IDirect3DPixelShader9*sun_sentinel_ps_=nullptr;
 enum class HdrState{Off,Active,Suspended};HdrState hdr_state_=HdrState::Active;renderer::HdrConfig hdr_config_;
 std::uint64_t id_=1,frame_=1,generation_=0;bool scene_open_=false;
 struct {unsigned NumSimultaneousRTs=3,VertexTextureFilterCaps=1,DevCaps2=1;}caps_;
 IDirect3DSurface9*hdr_main_=nullptr;Surface hdr_target_;Counters counters_;unsigned logged_failures_=0;bool states_invalidated=false;
 DWORD mip_bias_bits_=0,sampler_bound_mask_=0,sampler_biased_mask_=0;float mip_bias_=0;bool mip_bias_summary_logged_=false;
 unsigned mip_bias_total_sets_=0,mip_bias_total_restores_=0,mip_bias_total_reads_=0,mip_bias_total_game_writes_=0,mip_bias_total_failures_=0;
 DWORD mip_bias_game_write_stage_=0,mip_bias_game_write_value_=0;
 template<class F,class...A>HRESULT direct_call(unsigned slot,A...a)const{return native<F>(slot)(device_,a...);} // the route's value-only entry
 template<class F>F native(unsigned slot)const {
  switch(slot){case SetRenderTarget:return reinterpret_cast<F>(reinterpret_cast<void*>(set_target));case SetSamplerState:return reinterpret_cast<F>(reinterpret_cast<void*>(set_sampler));case SetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_vs));case SetPixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(set_ps));
  case CreateVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(create_vs));case CreatePixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(create_ps));
  case GetSamplerState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_sampler));case GetTexture:return reinterpret_cast<F>(reinterpret_cast<void*>(get_texture));
  case GetVertexShader:return reinterpret_cast<F>(reinterpret_cast<void*>(get_vs));case GetPixelShader:return reinterpret_cast<F>(reinterpret_cast<void*>(get_ps));
  case GetVertexShaderConstantF:case GetPixelShaderConstantF:return reinterpret_cast<F>(reinterpret_cast<void*>(get_f));
  case GetVertexShaderConstantI:return reinterpret_cast<F>(reinterpret_cast<void*>(get_i));
  case GetStreamSource:return reinterpret_cast<F>(reinterpret_cast<void*>(get_stream));case GetIndices:return reinterpret_cast<F>(reinterpret_cast<void*>(get_indices));
  case GetVertexDeclaration:return reinterpret_cast<F>(reinterpret_cast<void*>(get_declaration));case GetRenderTarget:return reinterpret_cast<F>(reinterpret_cast<void*>(get_target));
  case GetDepthStencilSurface:return reinterpret_cast<F>(reinterpret_cast<void*>(get_depth));case GetViewport:return reinterpret_cast<F>(reinterpret_cast<void*>(get_viewport));
  case GetDisplayMode:return reinterpret_cast<F>(reinterpret_cast<void*>(get_display_mode));
  case GetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_state));
  case GetTextureStageState:return reinterpret_cast<F>(reinterpret_cast<void*>(get_stage));
  case SetRenderState:return reinterpret_cast<F>(reinterpret_cast<void*>(set_state));default:return reinterpret_cast<F>(reinterpret_cast<void*>(set_constants));}
 }
 void set_stream_source(UINT,IDirect3DVertexBuffer9*,UINT,UINT){}void set_indices(IDirect3DIndexBuffer9*){}void set_vertex_declaration(IDirect3DVertexDeclaration9*){}
 void begin_frame(std::uint64_t,bool){}void resync_shadow()noexcept;void after_reset(HRESULT)noexcept;
 void begin_stateblock()noexcept;void end_stateblock()noexcept;void stateblock_applied()noexcept;
 void restore_bindings()noexcept;
 HRESULT restore_bindings_checked()noexcept;HRESULT restore_mip_bias()noexcept;void restore_mip_bias_stage(unsigned,HRESULT*)noexcept;
 HRESULT flush_bindings()noexcept;
 void set_texture(DWORD,IDirect3DBaseTexture9*,DWORD,bool,int=2)noexcept;int composition_texture_reader(DWORD,IDirect3DBaseTexture9*)noexcept;
 void prepare_composition(const MotionDrawCall&,MotionRoute&)noexcept;void finish_composition(HRESULT,renderer::LinearCompositionPolicy=renderer::LinearCompositionPolicy::AdditiveEmission)noexcept;bool publish_composition()noexcept;void begin_composition_frame()noexcept;void composition_export()noexcept;void release_composition_identity()noexcept;void before_texture_write(IDirect3DBaseTexture9*)noexcept;
 unsigned content_writes=0;IDirect3DSurface9*write_target=nullptr;void before_render_target_write(IDirect3DSurface9*surface){++content_writes;write_target=surface;hdr_state_=HdrState::Off;}
 unsigned evaluations=0;bool route_on_evaluation=false,recovery_reads_fail=false;
 HRESULT get_render_state_native(unsigned,DWORD*out){*out=0;return recovery_reads_fail?E_FAIL:S_OK;}
 MotionRoute before_draw(const MotionDrawCall&)noexcept;void evaluate_draw(const MotionDrawCall&,MotionRoute&r){++evaluations;if(route_on_evaluation)r.routed=true;}void log_mip_bias_game_write(){}void record(unsigned,std::uint64_t,bool=false){}
 bool scene_bound()const{return bound_scene;}void invalidate_taa(TaaInvalidateSite){++taa_invalidations;}
 bool reference_accounting_busy()const{return releasing_||taa_busy_||composition_busy_||(composition_&&composition_->reference_accounting_busy());}void before_reset()noexcept;
 void drop_direct(){} void drop_redirect(){release_composition_identity();} void release_target(){release(target_surface_);release(depth_surface_);}
 template<class F>void taa_call(F&&f){f();}void invalidate_render_states(){states_invalidated=true;}
 HRESULT bind_target(unsigned index,IUnknown*p){return set_target(device_,index,static_cast<IDirect3DSurface9*>(p));}
 DWORD dither_=0;HRESULT dither_result_=S_OK;unsigned dither_reads_=0;
 HRESULT render_state(unsigned index,DWORD*out){if(index==D3DRS_DITHERENABLE){++dither_reads_;*out=dither_;return dither_result_;}*out=device_->write_masks[index==D3DRS_COLORWRITEENABLE1?1:2];return S_OK;}
 HRESULT bind_targets(MotionRoute&)noexcept;HRESULT prepare_constants(MotionRoute&)noexcept;
 unsigned device_references()const noexcept;void release_resources()noexcept;
 void configure_linear_materials(bool,const renderer::LinearMaterialConfig&)noexcept;
 bool composition_requested()const noexcept{return linear_emission_requested_||distance_fade_requested_||screen_emission_requested_;}
 // Blend shadow is required by every route that reads the blend states
 // (composition producers, the source-gain lane and the additive option).
 bool blend_shadow_requested()const noexcept{return composition_requested()||emission_source_gain_requested_||screen_additive_requested_;}
 void configure_linear_distance_fade(bool)noexcept;void configure_linear_emissions(bool,float)noexcept;void refresh_linear_emission_contract()noexcept;void report_xt_default_unavailable()noexcept;
 void register_vertex_shader(IDirect3DVertexShader9*,const DWORD*,std::size_t,std::uint64_t)noexcept;
 void register_pixel_shader(IDirect3DPixelShader9*,const DWORD*,std::size_t,std::uint64_t)noexcept;
 void set_vertex_shader(IDirect3DVertexShader9*)noexcept;void set_pixel_shader(IDirect3DPixelShader9*)noexcept;
 void set_render_state(D3DRENDERSTATETYPE,DWORD)noexcept;
 void set_sampler_state(DWORD,D3DSAMPLERSTATETYPE,DWORD)noexcept;void resync_samplers()noexcept;
 void refresh_linear_material_contract()noexcept;unsigned linear_material_refusal()noexcept;HRESULT bind_variant_pair(MotionRoute&,bool)noexcept;HRESULT undo(MotionRoute&)noexcept;void rollback_route(MotionRoute&)noexcept;
 void count_material_route(const MotionRoute&)noexcept;
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
// The per-program sun register of the caster counter (shadow_replay_sun.h,
// shader_constant_register.h): stand-ins, the counter is off in this fixture.
namespace shadow_replay {constexpr unsigned sun_register_limit=32;constexpr const char*depth_sun_constant_name="LightDir_Dir0";}
namespace renderer {inline int shader_float_constant_register(const std::uint32_t*,std::size_t,const char*)noexcept{return -1;}}
namespace x3m {namespace renderer=::renderer;namespace fade_route=::fade_route;namespace shadow_replay=::shadow_replay;}
unsigned fade_witness_frames=0;bool shimmer_trace_requested=false,screen_emission_timing_requested=false;
bool linear_material_requested=false,motion_output_requested=true,hdr_requested=true,taa_requested=true,linear_distance_fade_requested=false,linear_emission_requested=false,screen_emission_requested=false;float emission_gain=1;float screen_emission_gain=1.f; // step E composition gain g, parsed by the extracted setting reader
float emission_source_gain=1.f; // X3M_EMISSION_SOURCE_GAIN, parsed by the same extracted setting reader
float hull_emission_gain=1.f;   // X3M_HULL_EMISSION_GAIN (emitter plan phase 3), same reader
float hull_lightmap_gain=1.f;   // X3M_HULL_LIGHTMAP_GAIN (hull-self-illumination.md 5), same reader
bool lightmap_far_fade_requested=false;float lightmap_far_fade[3]={0.f,0.f,1.f}; // X3M_LIGHT_MAP_FAR_FADE, same reader
bool screen_emission_additive_requested=false;float screen_emission_additive_gain=1.f; // X3M_SCREEN_EMISSION_ADDITIVE=G
bool screen_emission_additive_alpha_requested=false;float screen_emission_additive_alpha=1.f; // X3M_SCREEN_EMISSION_ADDITIVE_ALPHA=K, mirrored inertly
float original_fill=0.f; // X3M_ORIGINAL_FILL=K, parsed by the same extracted setting reader
unsigned fade_route_threshold=500;
renderer::LinearMaterialConfig linear_material_config;
renderer::HdrConfig hdr_config;
#include "linear_material_live_under_test_inc.h"
// The transport metadata shares the real sampler/technique cache lifecycle.
// Short synthetic IDs select contracts; actual 148-pair identities are checked
// by the independent pure-transformer fixture, not invented by this seam.
const renderer::LinearMaterialPairContract* released_contract=nullptr;
void check_empty_contract(){
 CHECK(released_contract && released_contract->sampler_mask==0 && !released_contract->bump && released_contract->scalar_transport_count==0);
 for(const auto& map:released_contract->scalar_transport)
  CHECK(!map.source_texcoord&&!map.source_component&&!map.destination_texcoord&&!map.destination_component);
}
void palette_contract_lifecycle(){
 Device device;MotionOutput m;m.configure_linear_materials(true,{});m.device_=&device;
 IDirect3DVertexShader9 vs,unknown;IDirect3DPixelShader9 boron,paranid,native;DWORD v=30,b=42,p=43,n=40;
 m.register_vertex_shader(&vs,&v,4,v);m.register_pixel_shader(&boron,&b,4,b);m.register_pixel_shader(&paranid,&p,4,p);m.register_pixel_shader(&native,&n,4,n);
 auto select=[&](IDirect3DPixelShader9* ps){device.bound_vs=&vs;device.bound_ps=ps;m.set_vertex_shader(&vs);m.set_pixel_shader(ps);};
 auto check=[&](unsigned count,unsigned source){const auto& c=m.shadow_.material_contract;CHECK(c.sampler_mask==31&&c.bump&&c.scalar_transport_count==count);
  for(unsigned i=0;i<count;++i){const auto& t=c.scalar_transport[i];CHECK(t.source_texcoord==source&&t.source_component==i&&t.destination_texcoord==1+i&&t.destination_component==3);}
  for(unsigned i=count;i<2;++i){const auto& t=c.scalar_transport[i];CHECK(!t.source_texcoord&&!t.source_component&&!t.destination_texcoord&&!t.destination_component);}};
 released_contract=&m.shadow_.material_contract;
 select(&boron);check(2,6);select(&paranid);check(1,7);select(&native);check(0,0);
 // Same pointers re-registered under another original identity replace every
 // field; a recorded setter must not disturb the active transported contract.
 select(&boron);m.register_pixel_shader(&boron,&p,4,p);check(1,7);m.register_pixel_shader(&boron,&b,4,b);check(2,6);
 m.begin_stateblock();const auto lookups=renderer::contract_lookups;m.set_pixel_shader(&paranid);check(2,6);CHECK(renderer::contract_lookups==lookups);m.end_stateblock();check(2,6);
 device.bound_ps=&paranid;m.stateblock_applied();check(1,7);device.fail_get_ps=true;m.stateblock_applied();check_empty_contract();device.fail_get_ps=false;m.after_reset(S_OK);check(1,7);
 select(&boron);m.set_vertex_shader(&unknown);check_empty_contract();m.set_vertex_shader(&vs);check(2,6);m.set_pixel_shader(nullptr);check_empty_contract();m.set_pixel_shader(&boron);check(2,6);
 // Early bound-registration exits clear scalar count and inactive bytes before
 // native Release callbacks, including both stage aliases and thrown transforms.
 for(bool vertex:{false,true})for(bool throws:{false,true}){
  select(&boron);release_check=check_empty_contract;renderer::throw_transform=throws;
  if(vertex)m.register_vertex_shader(&vs,throws?&v:nullptr,4,v);else m.register_pixel_shader(&boron,throws?&b:nullptr,4,b);
  release_check=nullptr;renderer::throw_transform=false;check_empty_contract();
  if(vertex)m.register_vertex_shader(&vs,&v,4,v);else m.register_pixel_shader(&boron,&b,4,b);check(2,6);
 }
 // Failed Reset cannot manufacture a new logical contract. Successful Reset
 // resynchronizes it from actual shaders, with failed getters clearing all bits.
 device.bound_ps=&paranid;m.after_reset(E_FAIL);check(2,6);m.after_reset(S_OK);check(1,7);
 device.fail_get_vs=true;m.after_reset(S_OK);check_empty_contract();device.fail_get_vs=false;m.after_reset(S_OK);check(1,7);
 m.release_resources();check_empty_contract();released_contract=nullptr;
}
// Cache lifecycle coverage uses real registration/setter/resync methods above.
void contract_lifecycle() {
 Device device;MotionOutput m;m.configure_linear_materials(true,{});m.device_=&device;
 release_bump=&m.shadow_.material_contract.bump;
 IDirect3DVertexShader9 base_vs,bump_vs,unknown_vs;
 IDirect3DPixelShader9 base_ps,bump_ps,asteroid_ps,negative_ps,unknown_ps;
 DWORD base_v=10,base_p=20,bump_v=30,bump_p=40,asteroid_p=41,negative_p=21;
 unsigned lookups=renderer::contract_lookups;
 m.register_vertex_shader(&base_vs,&base_v,4,10);m.register_pixel_shader(&base_ps,&base_p,4,20);
 m.register_vertex_shader(&bump_vs,&bump_v,4,30);m.register_pixel_shader(&bump_ps,&bump_p,4,40);
 m.register_pixel_shader(&asteroid_ps,&asteroid_p,4,41);
 m.register_pixel_shader(&negative_ps,&negative_p,4,21);
 CHECK(renderer::contract_lookups==lookups); // Unbound creation resolves no pair.
 auto select_default=[&]{device.bound_vs=&base_vs;device.bound_ps=&base_ps;m.set_vertex_shader(&base_vs);m.set_pixel_shader(&base_ps);};
 auto select_bump=[&]{device.bound_vs=&bump_vs;device.bound_ps=&bump_ps;m.set_vertex_shader(&bump_vs);m.set_pixel_shader(&bump_ps);};
 auto select_asteroid=[&]{device.bound_vs=&bump_vs;device.bound_ps=&asteroid_ps;m.set_vertex_shader(&bump_vs);m.set_pixel_shader(&asteroid_ps);};
 // Both four- and five-sampler BUMP techniques count, while the four-sampler
 // conventional DEFAULT does not. Failed/fallback draws do not count either.
 MotionRoute counted;counted.linear_material=true;
 select_default();CHECK(m.shadow_.material_contract.sampler_mask==15&&!m.shadow_.material_contract.bump);m.count_material_route(counted);
 select_asteroid();CHECK(m.shadow_.material_contract.sampler_mask==15&&m.shadow_.material_contract.bump);m.count_material_route(counted);
 select_bump();CHECK(m.shadow_.material_contract.sampler_mask==31&&m.shadow_.material_contract.bump);m.count_material_route(counted);
 counted.linear_material=false;m.count_material_route(counted);
 CHECK(m.counters_.material_routed==3&&m.counters_.material_bump_routed==2);
 // State-block application and failed resync change the cached technique
 // together with its mask, including equal-mask DEFAULT/BUMP transitions.
 select_default();device.bound_vs=&bump_vs;device.bound_ps=&asteroid_ps;m.stateblock_applied();
 CHECK(m.shadow_.material_contract.sampler_mask==15&&m.shadow_.material_contract.bump);
 device.fail_get_ps=true;m.stateblock_applied();CHECK(!m.shadow_.material_contract.sampler_mask&&!m.shadow_.material_contract.bump);
 device.fail_get_ps=false;m.after_reset(S_OK);CHECK(m.shadow_.material_contract.sampler_mask==15&&m.shadow_.material_contract.bump);
 for(bool vertex:{true,false}) {
  select_asteroid();release_mask=&m.shadow_.material_contract.sampler_mask;
  if(vertex)m.register_vertex_shader(&bump_vs,nullptr,4,30);else m.register_pixel_shader(&asteroid_ps,nullptr,4,41);
  release_mask=nullptr;CHECK(!m.shadow_.material_contract.sampler_mask&&!m.shadow_.material_contract.bump);
  if(vertex)m.register_vertex_shader(&bump_vs,&bump_v,4,30);else m.register_pixel_shader(&asteroid_ps,&asteroid_p,4,41);
  CHECK(m.shadow_.material_contract.sampler_mask==15&&m.shadow_.material_contract.bump);
 }
 select_default();CHECK(m.shadow_.material_contract.sampler_mask==15);
 device.fail_sampler=4;m.resync_samplers();CHECK(!m.samplers_[4].srgb_known&&m.linear_material_refusal()==0);
 select_bump();CHECK(m.shadow_.material_contract.sampler_mask==31&&m.linear_material_refusal()==4);
 m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,TRUE);CHECK(m.linear_material_refusal()==4);
 select_default();CHECK(m.linear_material_refusal()==0); // S4 TRUE is irrelevant to DEFAULT.
 select_bump();m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,FALSE);CHECK(m.linear_material_refusal()==0);
 unsigned reads=device.sampler_reads;lookups=renderer::contract_lookups;
 for(unsigned n=0;n<1000;++n)CHECK(m.linear_material_refusal()==0);
 CHECK(device.sampler_reads==reads&&renderer::contract_lookups==lookups);
 // Null, unknown and same-VS unreviewed PS setters all revoke admission.
 m.set_pixel_shader(&negative_ps);CHECK(m.shadow_.vs==&bump_vs&&m.shadow_.material_contract.sampler_mask==0);
 m.set_pixel_shader(&bump_ps);CHECK(m.shadow_.material_contract.sampler_mask==31);
 m.set_vertex_shader(nullptr);CHECK(m.shadow_.material_contract.sampler_mask==0);
 m.set_vertex_shader(&unknown_vs);CHECK(m.shadow_.material_contract.sampler_mask==0);
 m.set_vertex_shader(&bump_vs);CHECK(m.shadow_.material_contract.sampler_mask==31);
 m.set_pixel_shader(&unknown_ps);CHECK(m.shadow_.material_contract.sampler_mask==0);
 m.set_pixel_shader(nullptr);CHECK(m.shadow_.material_contract.sampler_mask==0);
 select_default();device.fail_sampler=-1;m.resync_samplers();
 // Recorded writes do not change active pair or S4. End/Apply pull actual
 // native state, including the current pair and five sampler decode values.
 m.begin_stateblock();lookups=renderer::contract_lookups;
 m.set_vertex_shader(&bump_vs);m.set_pixel_shader(&bump_ps);m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,TRUE);
 CHECK(m.shadow_.material_contract.sampler_mask==15&&!m.samplers_[4].srgb&&renderer::contract_lookups==lookups);
 m.end_stateblock();CHECK(m.shadow_.material_contract.sampler_mask==15&&m.linear_material_refusal()==0);
 device.bound_vs=&bump_vs;device.bound_ps=&bump_ps;device.srgb[4]=TRUE;m.stateblock_applied();
 CHECK(m.shadow_.material_contract.sampler_mask==31&&m.linear_material_refusal()==4);
 device.srgb[4]=FALSE;m.stateblock_applied();CHECK(m.shadow_.material_contract.sampler_mask==31&&m.linear_material_refusal()==0);
 // A failed shader getter during full resync cannot leave a stale contract.
 for(bool fail_vertex:{true,false}) {
  device.fail_get_vs=fail_vertex;device.fail_get_ps=!fail_vertex;m.stateblock_applied();CHECK(m.shadow_.material_contract.sampler_mask==0&&m.linear_material_refusal()==1);
  device.fail_get_vs=device.fail_get_ps=false;m.stateblock_applied();CHECK(m.shadow_.material_contract.sampler_mask==31);
 }
 // Successful Reset refreshes S4 and the pair without application setters.
 device.srgb[4]=TRUE;m.after_reset(S_OK);CHECK(m.shadow_.material_contract.sampler_mask==31&&m.linear_material_refusal()==4);
 device.srgb[4]=FALSE;m.after_reset(S_OK);CHECK(m.shadow_.material_contract.sampler_mask==31&&m.linear_material_refusal()==0);
 device.fail_get_vs=true;m.after_reset(S_OK);CHECK(m.shadow_.material_contract.sampler_mask==0);device.fail_get_vs=false;
 m.after_reset(S_OK);CHECK(m.shadow_.material_contract.sampler_mask==31);
 // Every bound-registration early exit/exception invalidates before releasing
 // old objects, and another stage's setter cannot resurrect that old mask.
 for(bool vertex:{true,false})for(unsigned failure=0;failure<9;++failure) {
  select_default();CHECK(m.shadow_.material_contract.sampler_mask==15);
  renderer::reject_row=failure==2;renderer::throw_transform=failure==4;renderer::reject_transform=failure==5;
  device.fail_motion_create=failure==6;m.enabled_=failure!=7;m.requested_=failure!=8;
  if(failure==3){if(vertex)m.vertex_.fail_next=true;else m.pixel_.fail_next=true;}
  release_mask=&m.shadow_.material_contract.sampler_mask;
  if(vertex)m.register_vertex_shader(&base_vs,failure==0?nullptr:&base_v,failure==1?3:4,10);
  else m.register_pixel_shader(&base_ps,failure==0?nullptr:&base_p,failure==1?3:4,20);
  CHECK(m.shadow_.material_contract.sampler_mask==0);
  release_mask=nullptr;m.enabled_=m.requested_=true;
  if(vertex)m.set_pixel_shader(&base_ps);else m.set_vertex_shader(&base_vs);
  CHECK(m.shadow_.material_contract.sampler_mask==0);
  renderer::reject_row=renderer::throw_transform=renderer::reject_transform=false;device.fail_motion_create=false;
  if(vertex)m.register_vertex_shader(&base_vs,&base_v,4,10);else m.register_pixel_shader(&base_ps,&base_p,4,20);
  CHECK(m.shadow_.material_contract.sampler_mask==15&&m.linear_material_refusal()==0);
 }
 // Material Create failure keeps the ordinary registered pair; combined
 // readiness stays live, separate from the exact cached sampler contract.
 device.fail_combined_create=true;release_mask=&m.shadow_.material_contract.sampler_mask;
 m.register_pixel_shader(&base_ps,&base_p,4,20);release_mask=nullptr;
 CHECK(m.shadow_.material_contract.sampler_mask==15&&m.linear_material_refusal()==2);
 device.fail_combined_create=false;m.register_pixel_shader(&base_ps,&base_p,4,20);
 CHECK(m.linear_material_refusal()==0);
 m.release_resources();CHECK(m.device_references()==0&&m.shadow_.material_contract.sampler_mask==0&&!m.shadow_.material_contract.bump);
 release_bump=nullptr;
 // Even actual setters and resync issue no contract lookup when disabled.
 MotionOutput off;off.device_=&device;lookups=renderer::contract_lookups;reads=device.sampler_reads;
 off.register_vertex_shader(&base_vs,&base_v,4,10);off.register_pixel_shader(&base_ps,&base_p,4,20);
 off.set_vertex_shader(&base_vs);off.set_pixel_shader(&base_ps);off.resync_shadow();
 CHECK(renderer::contract_lookups==lookups&&device.sampler_reads==reads&&off.shadow_.material_contract.sampler_mask==0);
 off.release_resources();
}

MotionOutput* retiring_emission=nullptr;
void writer_retirement_check(){CHECK(retiring_emission&&retiring_emission->composition_busy_&&retiring_emission->device_references()==0);}
void alias_retirement_check(){CHECK(retiring_emission&&retiring_emission->reference_accounting_busy()&&retiring_emission->device_references()==0&&!retiring_emission->composition_main_identity_&&!retiring_emission->composition_main_texture_&&!retiring_emission->composition_main_sampler_mask_);}
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
 CHECK(m.vertex_[&material_vs].variant&&m.vertex_[&material_vs].material_variant&&m.pixel_[&material_ps].variant&&m.pixel_[&material_ps].material_variant&&!m.pixel_[&material_ps].emission_variant&&m.device_references()==8);
 const auto released=releases;retiring_emission=&m;release_check=emission_retirement_check;m.release_resources();release_check=nullptr;retiring_emission=nullptr;
 CHECK(releases==released+8&&m.device_references()==0&&!m.shadow_.emission_eligible_variant);m.release_resources();CHECK(releases==released+8);
 std::printf("linear_emission_cache checks=%u\n",checks-checks_before);
}
// INSTANCE and DEFAULT can share the same augmented PS. The native VS needs
// no motion variant or object identity; exact pair and actual draw-state gates
// remain separate. Synthetic IDs keep this cache test independent of bytes.
void emission_instance_cache_cases(){
 const unsigned before=checks;Device device;MotionOutput m;m.configure_linear_emissions(true,1);m.device_=&device;
 IDirect3DVertexShader9 original,instance,unknown;IDirect3DPixelShader9 ps,cross;DWORD v=50,i=52,p=60,q=61;
 m.register_vertex_shader(&original,&v,4,v);m.register_vertex_shader(&instance,&i,4,i);
 m.register_pixel_shader(&ps,&p,4,p);m.register_pixel_shader(&cross,&q,4,q);
 auto select=[&](IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel){device.bound_vs=vertex;device.bound_ps=pixel;m.set_vertex_shader(vertex);m.set_pixel_shader(pixel);};
 select(&original,&ps);auto* shared=m.shadow_.emission_eligible_variant;CHECK(shared);
 select(&instance,&ps);CHECK(m.shadow_.emission_eligible_variant==shared&&!m.shadow_.vs_row&&!m.shadow_.vs_variant&&device.vs_creates==0&&device.emission_creates==2);
 const auto lookups=renderer::emission_lookups,transforms=renderer::emission_transforms;
 for(unsigned n=0;n<1000;++n)CHECK(m.shadow_.emission_eligible_variant==shared);
 CHECK(renderer::emission_lookups==lookups&&renderer::emission_transforms==transforms);
 select(&instance,&cross);CHECK(!m.shadow_.emission_eligible_variant);select(&unknown,&ps);CHECK(!m.shadow_.emission_eligible_variant);select(&instance,&ps);
 m.begin_stateblock();m.set_vertex_shader(&unknown);CHECK(m.shadow_.emission_eligible_variant==shared);m.end_stateblock();CHECK(m.shadow_.emission_eligible_variant==shared);
 device.bound_vs=&unknown;m.stateblock_applied();CHECK(!m.shadow_.emission_eligible_variant);device.bound_vs=&instance;m.stateblock_applied();CHECK(m.shadow_.emission_eligible_variant==shared);
 m.before_reset();CHECK(!m.shadow_.emission_eligible_variant);m.after_reset(E_FAIL);CHECK(!m.shadow_.emission_eligible_variant);m.after_reset(S_OK);CHECK(m.shadow_.emission_eligible_variant==shared);
 // Reusing the same COM pointer under an unsupported whole-original identity
 // clears the pair; registering it back recovers without touching the shared PS.
 m.register_vertex_shader(&instance,&i,4,53);CHECK(!m.shadow_.emission_eligible_variant);
 m.register_vertex_shader(&instance,&i,4,i);CHECK(m.shadow_.emission_eligible_variant==shared&&device.emission_creates==2);
 device.fail_get_vs=true;m.stateblock_applied();CHECK(!m.shadow_.emission_eligible_variant);device.fail_get_vs=false;m.stateblock_applied();CHECK(m.shadow_.emission_eligible_variant==shared);
 m.release_resources();CHECK(m.device_references()==0&&!m.shadow_.emission_eligible_variant);
 std::printf("linear_emission_instance_cache checks=%u\n",checks-before);
}
// Execute the capture environment parser, including strict opt-in/decode and
// finite gain validation, independently of the material feature request.
// Synthetic IDs exercise cache control flow. The separate transformer fixture
// owns real bytecode identity and six-pair shader correctness.
void distance_fade_cache_cases(){
 const unsigned before=checks;Device device;DWORD v=70,p=80;IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;
 // The reporting out-parameter is false on every non-applied path, including
 // an exception caught by the production registration boundary.
 {std::vector<std::uint32_t> output;renderer::LinearMaterialConfig config{};config.fill=.06f;bool applied=true;
  renderer::fade_reject=true;CHECK(renderer::linear_distance_fade_pixel_variant(&p,1,config,output,&applied)==renderer::LinearMaterialResult::UnsupportedShader&&!applied);renderer::fade_reject=false;
  DWORD unknown=99;applied=true;CHECK(renderer::linear_distance_fade_pixel_variant(&unknown,1,config,output,&applied)==renderer::LinearMaterialResult::UnsupportedShader&&!applied);
  config.fill=0;applied=true;CHECK(renderer::linear_distance_fade_pixel_variant(&p,1,config,output,&applied)==renderer::LinearMaterialResult::Applied&&!applied);
  config.fill=.06f;CHECK(renderer::linear_distance_fade_pixel_variant(&p,1,config,output,&applied)==renderer::LinearMaterialResult::Applied&&applied);
  applied=true;renderer::fade_throw=true;try{renderer::linear_distance_fade_pixel_variant(&p,1,config,output,&applied);CHECK(false);}catch(const std::bad_alloc&){CHECK(!applied);}renderer::fade_throw=false;}
 const auto transforms=renderer::fade_transforms,lookups=renderer::fade_lookups;
 {MotionOutput m;m.device_=&device;m.register_vertex_shader(&vs,&v,4,v);m.register_pixel_shader(&ps,&p,4,p);m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);
  CHECK(renderer::fade_transforms==transforms&&renderer::fade_lookups==lookups&&!m.shadow_.fade_sampler_mask);m.release_resources();}
 {MotionOutput m;m.configure_linear_distance_fade(true);CHECK(!m.distance_fade_requested_);m.configure_linear_materials(true,{});m.configure_linear_distance_fade(true);CHECK(m.composition_requested()&&!m.linear_emission_requested_);m.device_=&device;m.configure_linear_distance_fade(false);CHECK(m.distance_fade_requested_);}
 for(unsigned pair=0;pair<6;++pair){MotionOutput m;m.configure_linear_materials(true,{});m.configure_linear_distance_fade(true);m.device_=&device;v=70+pair;p=80+pair%4;
  m.register_vertex_shader(&vs,&v,4,v);m.register_pixel_shader(&ps,&p,4,p);m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);
  CHECK(m.shadow_.fade_sampler_mask==15&&m.shadow_.vs_fade_variant&&m.shadow_.ps_fade_variant&&!m.shadow_.emission_eligible_variant);
  auto* cached=m.shadow_.vs_fade_variant;const auto reads=device.sampler_reads,cache_lookups=renderer::fade_lookups,cache_transforms=renderer::fade_transforms,creates=device.vs_creates;
  for(unsigned n=0;n<1000;++n){m.before_draw({});CHECK(m.shadow_.fade_sampler_mask==15&&m.shadow_.vs_fade_variant==cached);}
  CHECK(device.sampler_reads==reads&&renderer::fade_lookups==cache_lookups&&renderer::fade_transforms==cache_transforms&&device.vs_creates==creates);
  m.set_vertex_shader(nullptr);CHECK(!m.shadow_.fade_sampler_mask&&!m.shadow_.vs_fade_variant);m.set_vertex_shader(&vs);
  // Pair recognition survives variant creation failure; required coverage must
  // not quietly disappear with a transient missing shader object.
  for(bool vertex:{true,false}){device.fail_program=(vertex?v:p)+600;device.partial_program=true;const auto released=releases;
   if(vertex)m.register_vertex_shader(&vs,&v,4,v);else m.register_pixel_shader(&ps,&p,4,p);
   CHECK(m.shadow_.fade_sampler_mask==15&&(vertex?!m.shadow_.vs_fade_variant:!m.shadow_.ps_fade_variant)&&releases>released);
   device.fail_program=0;device.partial_program=false;if(vertex)m.register_vertex_shader(&vs,&v,4,v);else m.register_pixel_shader(&ps,&p,4,p);
   CHECK(m.shadow_.vs_fade_variant&&m.shadow_.ps_fade_variant);}
  DWORD cross=80+(pair+1)%4;m.register_pixel_shader(&ps,&cross,4,cross);CHECK(!m.shadow_.fade_sampler_mask);m.register_pixel_shader(&ps,&p,4,p);CHECK(m.shadow_.fade_sampler_mask==15);
  device.bound_vs=&vs;device.bound_ps=&ps;cached=m.shadow_.vs_fade_variant;m.begin_stateblock();m.set_vertex_shader(nullptr);m.end_stateblock();CHECK(m.shadow_.vs_fade_variant==cached);
  const auto refs=m.device_references();m.before_reset();CHECK(!m.shadow_.fade_sampler_mask&&m.device_references()==refs);m.after_reset(E_FAIL);CHECK(!m.shadow_.fade_sampler_mask);m.after_reset(S_OK);CHECK(m.shadow_.fade_sampler_mask==15&&m.shadow_.vs_fade_variant==cached);
  m.release_resources();CHECK(!m.shadow_.fade_sampler_mask&&!m.shadow_.vs_fade_variant&&!m.shadow_.ps_fade_variant&&m.device_references()==0);
 }
 std::printf("linear_distance_fade_cache checks=%u\n",checks-before);
}
void distance_fade_route_cases(){
 const unsigned before=checks;Device device;IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;IDirect3DTexture9 main;IDirect3DSurface9 surface;surface.texture=&main;
 auto states=[&](MotionOutput&m){for(unsigned i=0;i<6;++i)m.shadow_.states_known[i]=true;
  const DWORD values[]={D3DZB_TRUE,FALSE,FALSE,TRUE,7,FALSE};for(unsigned i=0;i<6;++i)m.shadow_.states[i]=values[i];
  const DWORD blend[]={D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD};for(unsigned i=0;i<3;++i){m.shadow_.composition_blend_known[i]=true;m.shadow_.composition_blend[i]=blend[i];}
  for(auto&sampler:m.samplers_){sampler.srgb_known=true;sampler.srgb=FALSE;}
 };
 auto ready=[&](MotionOutput&m){m.device_=&device;m.distance_fade_requested_=m.linear_material_requested_=true;m.scene_open_=true;m.composition_effective_=true;m.composition_required_producers_=2;m.composition_readers_known_=true;m.hdr_main_=&surface;
  m.shadow_.fade_sampler_mask=15;m.shadow_.vs_fade_variant=&vs;m.shadow_.ps_fade_variant=&ps;m.shadow_.vs_row=&renderer::row;m.composition_=std::make_unique<renderer::LinearEmissionPass>();m.composition_->cap.supported_policies=m.composition_->cap.available_policies=2;states(m);
 };
 // Both producers share one frame clear, pool owner, identity and mask.
 for(bool emissions:{false,true}){MotionOutput m;ready(m);m.linear_emission_requested_=emissions;auto&pass=*m.composition_;pass.cap.supported_policies=pass.cap.available_policies=emissions?3:2;m.composition_attach_attempted_=false;
  m.begin_composition_frame();CHECK(pass.attaches==1&&pass.requested_policies==(emissions?7u:6u)&&pass.clears==1&&m.composition_required_producers_==(emissions?3u:2u)&&!m.composition_frame_stopped_);states(m);
  auto route=m.before_draw({});CHECK(route.composition&&route.composition_policy==renderer::LinearCompositionPolicy::DistanceFade&&pass.boundary.augmented_vertex==&vs&&pass.boundary.augmented==&ps);
  m.finish_composition(S_OK,route.composition_policy);CHECK(m.composition_counts_.linear_fade==1&&m.composition_counts_.pool_traffic_bytes==64u*32u*56u);
  if(emissions){m.shadow_.fade_sampler_mask=0;m.shadow_.emission_pair=true;m.shadow_.emission_eligible_variant=&ps;route=m.before_draw({});CHECK(route.composition&&route.composition_policy==renderer::LinearCompositionPolicy::AdditiveEmission&&pass.boundary.augmented_vertex==nullptr);m.finish_composition(S_OK,route.composition_policy);}
  CHECK(pass.clears==1&&pass.finishes==(emissions?2u:1u)&&pass.prepares==pass.finishes&&m.composition_.get()==&pass&&m.composition_counts_.linear==(emissions?2u:1u));m.release_resources();CHECK(main.refs==1);
 }
 // Composition consumes successful setters even when the optional ordinary
 // motion state-shadow optimization is disabled. Recording cannot mutate it.
 {MotionOutput m;ready(m);m.state_shadow_=false;auto&pass=*m.composition_;
  auto draw=[&](bool expected){auto route=m.before_draw({});CHECK(route.submit&&route.composition==expected&&!m.composition_frame_stopped_);if(route.composition)m.finish_composition(S_OK,route.composition_policy);};
  draw(true);m.set_render_state(D3DRS_ZWRITEENABLE,TRUE);m.set_render_state(D3DRS_ALPHABLENDENABLE,FALSE);draw(false);
  m.set_render_state(D3DRS_ZWRITEENABLE,FALSE);m.set_render_state(D3DRS_ALPHABLENDENABLE,TRUE);draw(true);
  m.set_render_state(D3DRS_SRCBLEND,TRUE);draw(false);m.set_render_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);draw(true);
  m.begin_stateblock();m.set_render_state(D3DRS_ZWRITEENABLE,TRUE);m.set_render_state(D3DRS_SRCBLEND,TRUE);
  CHECK(m.shadow_.states[1]==FALSE&&m.shadow_.composition_blend[0]==D3DBLEND_SRCALPHA);m.end_stateblock();
  CHECK(!m.state_shadow_&&!m.shadow_.recording&&pass.prepares==3);
 }
 // Adapter-query failures keep the requested producer required and stop the
 // frame. Only a new frame retries; successful format discovery is cached.
 for(bool zero_format:{false,true}){MotionOutput m;ready(m);auto&pass=*m.composition_;m.composition_adapter_format_=D3DFMT_UNKNOWN;m.composition_attach_attempted_=false;m.composition_effective_=false;m.composition_required_producers_=0;
  device.display_mode_result=zero_format?S_OK:E_FAIL;device.display_mode_format=zero_format?D3DFMT_UNKNOWN:1;const auto reads=device.display_mode_reads;
  m.begin_composition_frame();CHECK(device.display_mode_reads==reads+1&&pass.attaches==0&&m.composition_required_producers_==2&&!m.composition_effective_&&m.composition_frame_stopped_&&m.taa_invalidations==1&&!m.composition_busy_);
  for(unsigned n=0;n<3;++n)CHECK(!m.before_draw({}).composition);CHECK(device.display_mode_reads==reads+1&&pass.attaches==0);
  device.display_mode_result=S_OK;device.display_mode_format=1;++m.frame_;m.begin_composition_frame();states(m);
  CHECK(device.display_mode_reads==reads+2&&m.composition_adapter_format_==1&&pass.attaches==1&&!m.composition_frame_stopped_);
  auto route=m.before_draw({});CHECK(route.composition);m.finish_composition(S_OK,route.composition_policy);++m.frame_;m.begin_composition_frame();
  CHECK(device.display_mode_reads==reads+2&&pass.attaches==1);
  m.before_reset();CHECK(m.composition_adapter_format_==D3DFMT_UNKNOWN&&main.refs==1);device.display_mode_format=3;m.after_reset(S_OK);CHECK(device.display_mode_reads==reads+2);
  ++m.frame_;m.begin_composition_frame();CHECK(device.display_mode_reads==reads+3&&m.composition_adapter_format_==3&&pass.attaches==2&&pass.attached_adapter_format==3);
  const auto reset_reads=device.display_mode_reads;m.before_draw({});CHECK(device.display_mode_reads==reset_reads);m.release_resources();CHECK(main.refs==1);
 }
 {MotionOutput m;ready(m);auto&pass=*m.composition_;m.composition_adapter_format_=D3DFMT_UNKNOWN;m.composition_attach_attempted_=false;m.composition_effective_=false;m.composition_required_producers_=0;
  device.display_mode_result=E_FAIL;const auto reads=device.display_mode_reads;m.begin_composition_frame();CHECK(m.composition_required_producers_==2&&!m.composition_effective_&&m.taa_invalidations==1&&pass.attaches==0);
  device.display_mode_result=S_OK;pass.cap.enabled=false;pass.cap.supported_policies=pass.cap.available_policies=0;++m.frame_;m.begin_composition_frame();
  CHECK(device.display_mode_reads==reads+2&&pass.attaches==1&&m.composition_required_producers_==0&&!m.composition_effective_&&m.taa_invalidations==1);
  auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&m.taa_invalidations==1&&device.display_mode_reads==reads+2);m.release_resources();
 }
 // Impl allocation can fail before a capability snapshot exists. Required
 // producers stay unresolved until a later frame successfully attaches.
 {MotionOutput m;ready(m);auto&pass=*m.composition_;m.composition_attach_attempted_=false;m.composition_effective_=false;m.composition_required_producers_=0;
  pass.attach_result=E_OUTOFMEMORY;pass.cap.enabled=false;pass.cap.supported_policies=pass.cap.available_policies=0;
  m.begin_composition_frame();CHECK(pass.attaches==1&&m.composition_attach_result_==E_OUTOFMEMORY&&m.composition_required_producers_==2&&!m.composition_effective_&&m.composition_frame_stopped_&&m.taa_invalidations==1&&pass.begins==0);
  for(unsigned n=0;n<3;++n){auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&m.composition_frame_stopped_);}
  CHECK(pass.attaches==1&&pass.prepares==0&&m.composition_counts_.linear==0&&m.composition_counts_.native==0);
  pass.attach_result=S_OK;pass.cap.enabled=true;pass.cap.supported_policies=pass.cap.available_policies=2;++m.frame_;m.begin_composition_frame();states(m);
  CHECK(pass.attaches==2&&m.composition_attach_result_==S_OK&&m.composition_required_producers_==2&&m.composition_effective_&&!m.composition_frame_stopped_&&pass.begins==1);
  auto route=m.before_draw({});CHECK(route.composition);m.finish_composition(S_OK,route.composition_policy);CHECK(pass.attaches==2&&m.composition_counts_.linear_fade==1);m.release_resources();CHECK(main.refs==1);
 }
 {MotionOutput m;ready(m);auto&pass=*m.composition_;m.composition_attach_attempted_=false;m.composition_effective_=false;m.composition_required_producers_=0;
  pass.attach_result=E_OUTOFMEMORY;pass.cap.enabled=false;pass.cap.supported_policies=pass.cap.available_policies=0;
  m.begin_composition_frame();CHECK(pass.attaches==1&&m.composition_required_producers_==2&&!m.composition_effective_&&m.composition_frame_stopped_&&m.taa_invalidations==1);
  pass.attach_result=S_OK;++m.frame_;m.begin_composition_frame();CHECK(pass.attaches==2&&m.composition_attach_result_==S_OK&&m.composition_required_producers_==0&&!m.composition_effective_&&m.taa_invalidations==1);
  auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&pass.attaches==2&&pass.prepares==0&&m.taa_invalidations==1);m.release_resources();
 }
 // Every exact state term is consumed; other blend/depth/write states remain
 // ordinary without an otherwise irrelevant producer poisoning the frame.
 for(unsigned changed=0;changed<9;++changed){MotionOutput m;ready(m);if(changed<6)m.shadow_.states[changed]^=1;else m.shadow_.composition_blend[changed-6]^=1;
  auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&!m.composition_frame_stopped_&&m.composition_->prepares==0);}
 {MotionOutput m;ready(m);m.shadow_.fade_sampler_mask=0;auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&!m.composition_frame_stopped_);}
 // Required producer refusals cannot be healed by a later eligible draw.
 for(unsigned failure=0;failure<12;++failure){MotionOutput m;ready(m);MotionDrawCall call;
  switch(failure){case 0:m.shadow_.vs_fade_variant=nullptr;break;case 1:m.shadow_.ps_fade_variant=nullptr;break;case 2:m.samplers_[0].srgb_known=false;break;case 3:m.samplers_[1].srgb=TRUE;break;
   case 4:m.shadow_.states_known[0]=false;break;case 5:m.shadow_.composition_blend_known[0]=false;break;case 6:call.composition_permission=false;break;case 7:m.composition_readers_known_=false;break;
   case 8:m.composition_main_sampler_mask_=1;break;case 9:m.composition_->cap.available_policies=0;break;case 10:m.shadow_.vs_row=nullptr;break;case 11:renderer::row.light_loop_bound_required=true;m.shadow_.integer0_known=true;m.shadow_.integer0[0]=9;break;}
  auto route=m.before_draw(call);CHECK(route.submit&&!route.composition&&m.composition_frame_stopped_&&m.taa_invalidations>0&&m.composition_->prepares==0);
  states(m);m.shadow_.vs_fade_variant=&vs;m.shadow_.ps_fade_variant=&ps;m.shadow_.vs_row=&renderer::row;renderer::row.light_loop_bound_required=false;m.composition_readers_known_=true;m.composition_main_sampler_mask_=0;m.composition_->cap.available_policies=2;
  route=m.before_draw({});CHECK(route.submit&&!route.composition&&m.composition_->prepares==0&&m.composition_frame_stopped_);
 }
 for(unsigned failure=0;failure<3;++failure){MotionOutput m;ready(m);auto&pass=*m.composition_;
  if(failure==0){pass.cap.enabled=false;pass.cap.available_policies=0;}else if(failure==1)pass.ensure_result=E_FAIL;else pass.begin={false,true,S_OK,E_FAIL,S_OK};
  m.begin_composition_frame();CHECK(m.composition_effective_&&m.composition_required_producers_==2&&m.composition_frame_stopped_&&m.taa_invalidations>0);states(m);pass.cap.enabled=true;pass.cap.available_policies=2;pass.ensure_result=S_OK;pass.begin={};
  CHECK(!m.before_draw({}).composition&&pass.prepares==0);m.release_resources();CHECK(main.refs==1);
 }
 {MotionOutput m;ready(m);m.composition_effective_=false;auto&pass=*m.composition_;pass.cap.enabled=false;pass.cap.supported_policies=pass.cap.available_policies=0;m.begin_composition_frame();CHECK(!m.composition_effective_&&m.composition_required_producers_==0&&m.taa_invalidations==0&&pass.begins==0);CHECK(!m.before_draw({}).composition&&m.taa_invalidations==0);}
 // Clean internal failure preserves accumulated mask bytes, while the caller
 // marks the required coverage incomplete and never prepares again this frame.
 {MotionOutput m;ready(m);auto&pass=*m.composition_;pass.mask_bytes={3,7,11,19};const auto bytes=pass.mask_bytes;pass.preparation={false,true,S_OK,E_FAIL,S_OK};
  auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&pass.mask_bytes==bytes&&m.composition_frame_stopped_&&m.taa_invalidations==1);pass.preparation={};CHECK(!m.before_draw({}).composition&&pass.prepares==1&&pass.mask_bytes==bytes);}
 // Preserve the first preparation failure even if restoration fails too.
 for(unsigned first=0;first<3;++first){MotionOutput m;ready(m);auto&pass=*m.composition_;pass.preparation={false,false,first==0?-71:S_OK,first<=1?-72:S_OK,-73};
  auto route=m.before_draw({});CHECK(!route.submit&&!route.composition&&route.submission_error==(first==0?-71:first==1?-72:-73)&&m.composition_state_lost_&&pass.prepares==1);CHECK(!m.before_draw({}).submit&&pass.prepares==1);}
 // Fade uses the same source-result/publication transaction and quarantine.
 for(unsigned failure=0;failure<4;++failure){MotionOutput m;ready(m);auto&pass=*m.composition_;auto route=m.before_draw({});CHECK(route.composition);HRESULT source=S_OK;
  if(failure==0){source=-117;pass.completion.image=renderer::LinearEmissionImage::Incomplete;}if(failure==1)m.hdr_->exchange_results={E_FAIL,S_OK};if(failure==2)pass.ack_result=E_FAIL;if(failure==3){pass.completion.restore=-119;pass.completion.image=renderer::LinearEmissionImage::Incomplete;}
  m.finish_composition(source,route.composition_policy);CHECK(pass.finishes==1&&pass.last_source==source&&m.composition_counts_.source==source&&!m.composition_busy_);
  if(failure==1)CHECK(pass.recoveries==1&&m.composition_counts_.native==1&&m.composition_counts_.linear_fade==0);else CHECK(m.composition_frame_stopped_&&m.composition_counts_.incomplete==1&&m.taa_invalidations>0);
  if(failure==2)CHECK(m.composition_state_lost_&&!m.before_draw({}).submit);
 }
 // A failed first restore invalidates M. Publishing recovered B restores
 // the source image and state, but cannot certify coverage for either policy.
 for(bool emission:{false,true}){MotionOutput m;ready(m);auto&pass=*m.composition_;auto*old=m.hdr_->target();auto*candidate=pass.candidate;
  if(emission){m.linear_emission_requested_=true;m.shadow_.fade_sampler_mask=0;m.shadow_.emission_pair=true;m.shadow_.emission_eligible_variant=&ps;m.composition_required_producers_=3;pass.cap.available_policies=pass.cap.supported_policies=3;}
  auto route=m.before_draw({});CHECK(route.composition&&route.composition_policy==(emission?renderer::LinearCompositionPolicy::AdditiveEmission:renderer::LinearCompositionPolicy::DistanceFade));
  pass.completion={renderer::LinearEmissionImage::Incomplete,S_OK,S_OK,-119,false};m.finish_composition(S_OK,route.composition_policy);
  CHECK(pass.finishes==1&&pass.recoveries==1&&pass.last_source==S_OK&&m.composition_counts_.source==S_OK&&m.composition_counts_.restore==S_OK);
  CHECK(m.hdr_->target()==candidate&&pass.candidate==old&&m.hdr_->exchange_calls==1&&pass.exchanges==std::vector<bool>({true}));
  CHECK(m.composition_counts_.incomplete==1&&m.composition_counts_.native==0&&m.composition_frame_stopped_&&!pass.coverage_valid()&&!m.composition_state_lost_&&m.taa_invalidations==1);
  auto next=m.before_draw({});CHECK(next.submit&&!next.composition&&pass.prepares==1&&m.composition_frame_stopped_);
 }
 {MotionOutput m;ready(m);m.composition_published_=true;m.composition_enhanced_=true;m.composition_readers_known_=false;auto route=m.before_draw({});CHECK(!route.composition&&m.composition_quarantined_&&m.composition_frame_stopped_);m.before_reset();CHECK(m.composition_quarantined_&&!m.composition_state_lost_);}
 std::printf("linear_distance_fade_route checks=%u\n",checks-before);
}

void emission_environment_cases(){
 environment.clear();environment[L"X3M_HDR_TONEMAP"]=L"agx";configure_environment();CHECK(!linear_emission_requested&&emission_gain==1);
 environment[L"X3M_LINEAR_EMISSIONS"]=L"1";
 for(const wchar_t*bad:{L"",L"nan",L"inf",L"-inf",L"-1",L"16.01",L"abc",L"1x",L"1 ",L"1111111111111111111111111111111111"}){environment[L"X3M_EMISSION_GAIN"]=bad;configure_environment();CHECK(!linear_emission_requested);}
 for(const wchar_t*good:{L"0",L"1",L"4",L"16"}){environment[L"X3M_EMISSION_GAIN"]=good;configure_environment();CHECK(linear_emission_requested&&emission_gain==std::wcstof(good,nullptr)&&!linear_material_requested);}
 environment.erase(L"X3M_EMISSION_GAIN");
 for(bool*dependency:{&taa_requested,&motion_output_requested,&hdr_requested}){*dependency=false;configure_environment();CHECK(!linear_emission_requested);*dependency=true;}
 for(const wchar_t*bad:{L"none",L"srgb",L"unknown"}){environment[L"X3M_HDR_DECODE"]=bad;configure_environment();CHECK(!linear_emission_requested);}environment.erase(L"X3M_HDR_DECODE");
 environment[L"X3M_HDR_TONEMAP"]=L"identity";configure_environment();CHECK(!linear_emission_requested);environment[L"X3M_HDR_TONEMAP"]=L"agx";
 environment[L"X3M_MATERIAL_DIRECT_GAIN"]=L"bad";configure_environment();CHECK(linear_emission_requested);environment.erase(L"X3M_MATERIAL_DIRECT_GAIN");
 environment[L"X3M_LINEAR_EMISSIONS"]=L"0";configure_environment();CHECK(!linear_emission_requested);
}
// Transaction decisions execute production methods; the pass/HDR doubles only
// script external results. No source draw exists here, so geometry is never replayed.
void distance_fade_environment_cases(){
 const unsigned before=checks;environment.clear();environment[L"X3M_HDR_TONEMAP"]=L"agx";configure_environment();CHECK(!linear_distance_fade_requested);
 environment[L"X3M_LINEAR_DISTANCE_FADE"]=L"1";configure_environment();CHECK(!linear_distance_fade_requested);
 environment[L"X3M_LINEAR_MATERIALS"]=L"1";configure_environment();CHECK(linear_distance_fade_requested&&!linear_emission_requested);
 for(bool*dependency:{&taa_requested,&motion_output_requested,&hdr_requested}){*dependency=false;configure_environment();CHECK(!linear_distance_fade_requested);*dependency=true;}
 for(const wchar_t*bad:{L"none",L"srgb",L"unknown"}){environment[L"X3M_HDR_DECODE"]=bad;configure_environment();CHECK(!linear_distance_fade_requested);}environment.erase(L"X3M_HDR_DECODE");
 environment[L"X3M_HDR_TONEMAP"]=L"identity";configure_environment();CHECK(!linear_distance_fade_requested);environment[L"X3M_HDR_TONEMAP"]=L"agx";
 environment[L"X3M_MATERIAL_DIRECT_GAIN"]=L"bad";configure_environment();CHECK(!linear_distance_fade_requested);environment.erase(L"X3M_MATERIAL_DIRECT_GAIN");
 environment[L"X3M_LINEAR_EMISSIONS"]=L"1";configure_environment();CHECK(linear_distance_fade_requested&&linear_emission_requested);
 for(const wchar_t*bad:{L"0",L"",L"true",L"11"}){environment[L"X3M_LINEAR_DISTANCE_FADE"]=bad;configure_environment();CHECK(!linear_distance_fade_requested);}
 environment.clear();std::printf("linear_distance_fade_environment checks=%u\n",checks-before);
}

void emission_route_cases(){
 const unsigned before=checks;
 // Motion rollback quarantine uses the same early native-submission boundary
 // even when supplemental emission is disabled. No native draw is replayed.
 for(bool emission:{false,true}){MotionOutput m;m.linear_emission_requested_=emission;m.motion_state_lost_=true;m.motion_state_error_=-91;
  auto r=m.before_draw({});CHECK(!r.submit&&!r.evaluated&&!r.composition&&r.submission_error==-91);CHECK(m.counters_.gates[1]==1&&m.taa_invalidations==1);
  m.after_reset(E_FAIL);CHECK(m.motion_state_lost_);auto again=m.before_draw({});CHECK(!again.submit&&again.submission_error==-91);
 }
 Device device;IDirect3DPixelShader9 shader;
 auto ready=[&](MotionOutput&m){m.device_=&device;device.target_result=S_OK;m.linear_emission_requested_=true;m.composition_effective_=true;m.scene_open_=true;m.shadow_.emission_pair=true;m.shadow_.emission_eligible_variant=&shader;m.composition_readers_known_=true;m.composition_=std::make_unique<renderer::LinearEmissionPass>();};
 for(unsigned refusal=0;refusal<23;++refusal){
  MotionOutput m;ready(m);MotionDrawCall call;auto*pass=m.composition_.get();auto*target=m.hdr_->target();
  switch(refusal){case 0:m.linear_emission_requested_=false;break;case 1:m.shadow_.emission_pair=false;m.shadow_.emission_eligible_variant=nullptr;break;
   case 2:call.composition_permission=false;break;case 3:call.indexed=false;break;case 4:call.user_memory=true;break;case 5:call.primitives=0;break;
   case 6:m.scene_open_=false;break;case 7:m.active_queries_=1;break;case 8:m.shadow_.recording=true;break;case 9:m.bound_scene=false;break;
   case 10:m.main_msaa_=true;break;case 11:m.taa_enabled_=false;break;case 12:m.hdr_enabled_=false;break;case 13:m.hdr_state_=MotionOutput::HdrState::Suspended;break;
   case 14:m.hdr_->active=false;break;case 15:m.hdr_config_.decode=x3::temporal::AgxDecode::srgb;break;case 16:m.hdr_config_.tonemap=renderer::HdrTonemap::Identity;break;
   case 17:pass->cap.enabled=false;pass->cap.available_policies=0;break;case 18:m.composition_frame_stopped_=true;break;case 19:m.composition_quarantined_=true;break;
   case 20:m.composition_readers_known_=false;break;case 21:m.composition_main_sampler_mask_=1;break;case 22:m.composition_busy_=true;break;}
  MotionRoute route;m.prepare_composition(call,route);CHECK(route.submit&&!route.composition);CHECK(pass->prepares==0&&pass->coverage_valid()&&pass->begins==0);CHECK(m.hdr_->target()==target&&m.hdr_->exchange_calls==0);
  const unsigned reason=refusal==1?0:refusal<=10?1:refusal<=17||refusal==22?2:refusal<=19?4:3;CHECK(refusal==0?m.composition_counts_.refused==0:m.composition_counts_.refused==1&&m.composition_counts_.refusal[reason]==1);
 }
 // Refused internal setup retains the already accumulated M; lost state stops
 // submission and the next before_draw never evaluates or prepares another pass.
 for(bool state:{true,false}){MotionOutput m;ready(m);auto&p=*m.composition_;p.preparation={false,state,S_OK,E_FAIL,-77};auto route=m.before_draw({});
  CHECK(!route.composition&&route.submit==state&&p.prepares==1&&p.coverage_valid());CHECK(!m.composition_busy_&&m.composition_state_lost_==!state);CHECK(m.composition_counts_.prepare_failures==1&&m.composition_counts_.refusal[5]==1&&m.composition_counts_.prepare==E_FAIL&&m.composition_counts_.prepare_restore==-77);
  if(!state){CHECK(route.submission_error==E_FAIL&&m.taa_invalidations==1);auto next=m.before_draw({});CHECK(!next.submit&&!next.evaluated&&p.prepares==1&&m.composition_counts_.suppressed==2);}
 }
 for(unsigned outcome=0;outcome<9;++outcome){
  MotionOutput m;ready(m);auto&p=*m.composition_;auto*old=m.hdr_->target();auto*candidate=p.candidate;auto route=m.before_draw({});
  CHECK(route.composition&&route.submit&&m.composition_busy_);CHECK(p.boundary.scene==old&&p.boundary.augmented==&shader&&p.boundary.frame==m.frame_&&p.boundary.admitted);
  CHECK(m.device_references()==0);HRESULT source=S_OK;
  if(outcome==1){source=-117;p.completion.image=renderer::LinearEmissionImage::Incomplete;}
  if(outcome==8)p.candidate->describable=false;
  if(outcome==7){source=-118;p.completion.image=renderer::LinearEmissionImage::Incomplete;m.hdr_->exchange_results={E_FAIL,S_OK};}
  if(outcome==2){m.hdr_->exchange_results={E_FAIL,S_OK};}
  if(outcome==3){p.ack_result=E_FAIL;}
  if(outcome==4){m.hdr_->exchange_results={E_FAIL,E_FAIL};}
  if(outcome==5){p.completion.image=renderer::LinearEmissionImage::Native;p.completion.composition=E_FAIL;}
  if(outcome==6){p.candidate_available=false;p.recovery.candidate_bound=false;}
  m.finish_composition(source);CHECK(p.finishes==1&&p.last_source==source&&m.composition_counts_.source==source&&!m.composition_busy_);
  if(outcome==0){CHECK(m.hdr_->target()==candidate&&p.candidate==old&&m.hdr_dirty_);CHECK(m.composition_counts_.linear==1&&m.composition_enhanced_&&m.taa_invalidations==0&&p.recoveries==0);}
  if(outcome==1){CHECK(m.composition_counts_.incomplete==1&&m.composition_frame_stopped_&&!p.coverage_valid());CHECK(m.taa_invalidations==1&&p.recoveries==0&&!m.composition_enhanced_);}
  if(outcome==2){CHECK(m.composition_counts_.exchange_failures==1&&m.composition_counts_.exchange==S_OK&&m.composition_counts_.exchanged==1);CHECK(m.hdr_->exchange_calls==2&&p.recoveries==1&&p.exchanges==std::vector<bool>({false,true}));CHECK(m.composition_counts_.native==1&&!m.composition_state_lost_&&!m.composition_enhanced_);}
  if(outcome==3){CHECK(m.composition_counts_.ack_failures==1&&m.composition_counts_.ack==E_FAIL);CHECK(m.hdr_->target()==candidate&&m.hdr_->exchange_calls==1&&p.recoveries==0&&p.acks==1);CHECK(m.composition_state_lost_&&m.composition_frame_stopped_&&m.composition_counts_.incomplete==1);}
  if(outcome==4){CHECK(p.recoveries==1&&m.hdr_->exchange_calls==2&&m.composition_state_lost_&&m.taa_invalidations==1);}
  if(outcome==5){CHECK(m.composition_counts_.native==1&&p.recoveries==0&&!m.composition_enhanced_&&p.coverage_valid()&&!m.composition_frame_stopped_&&m.composition_counts_.composition_failures==1);}
  if(outcome==8){CHECK(m.hdr_->target()==candidate&&p.candidate==old&&m.hdr_->exchange_calls==1&&p.acks==1&&p.exchanges[0]);CHECK(!m.hdr_target_.known&&m.composition_counts_.exchanged==1&&m.composition_state_lost_&&m.composition_frame_stopped_&&m.composition_counts_.incomplete==1&&m.taa_invalidations==1&&p.recoveries==0);}
  if(outcome==7){CHECK(p.recoveries==1&&p.finishes==1&&m.composition_counts_.incomplete==1&&m.composition_frame_stopped_&&!m.composition_enhanced_);}
  if(outcome==6){CHECK(p.recoveries==1&&m.hdr_->exchange_calls==0&&m.composition_state_lost_);}
  if(m.composition_state_lost_){auto next=m.before_draw({});CHECK(!next.submit&&!next.evaluated&&p.prepares==1&&p.finishes==1);}
 }
 // First restoration error survives mip/lazy cleanup; all owned lazy
 // changes are attempted, but the original draw is suppressed after uncertainty.
 for(unsigned fault=1;fault<3;++fault){MotionOutput m;ready(m);m.lazy_rt1_=true;
  if(fault==1){m.sampler_biased_mask_=1;m.samplers_[0].biased=true;m.samplers_[0].saved_known=true;device.ordinal=0;device.failed_calls={1};}
  if(fault==2)device.target_result=-43;
  auto route=m.before_draw({});CHECK(!route.submit&&!route.composition&&m.composition_state_lost_&&m.composition_->prepares==0);
  CHECK(!m.lazy_rt1_&&m.sampler_biased_mask_==(fault==1?1u:0u)&&m.sampler_restore_failed_mask_==(fault==1?1u:0u));
  CHECK(route.submission_error==(fault==1?E_FAIL:-43));device.failed_calls.clear();
 }
 // Empty frames still clear M once at the frame boundary. Failed allocation or
 // clear never reaches preparation and invalidates history.
 for(unsigned failure=0;failure<4;++failure){MotionOutput m;ready(m);IDirect3DTexture9 texture;IDirect3DSurface9 surface;surface.texture=&texture;m.hdr_main_=&surface;auto&p=*m.composition_;
  if(failure==1)p.ensure_result=E_FAIL;if(failure==2)p.begin={false,true,S_OK,E_FAIL,S_OK};if(failure==3)p.begin={false,false,S_OK,E_FAIL,E_FAIL};
  m.begin_composition_frame();CHECK(p.ensures==1&&p.begins==(failure==1?0u:1u)&&p.prepares==0&&!m.composition_busy_);
  CHECK(m.composition_frame_stopped_==(failure!=0)&&m.composition_state_lost_==(failure==3));CHECK(m.taa_invalidations==(failure?1u:0u));
  CHECK(m.composition_main_texture_==&texture&&m.composition_main_identity_==&texture&&m.composition_readers_known_);
  CHECK(m.device_references()==7);retiring_emission=&m;release_check=alias_retirement_check;m.drop_redirect();release_check=nullptr;retiring_emission=nullptr;CHECK(texture.refs==1&&!m.composition_busy_);
 }
 // Canonical alias detection covers all pixel, vertex and displacement slots;
 // unchanged setters use the cache, recording cannot change effective readers.
 {MotionOutput m;ready(m);IUnknown identity;IDirect3DTexture9 main;main.identity=&identity;IDirect3DBaseTexture9 alias;alias.identity=&identity;m.composition_main_identity_=&identity;m.resync_samplers();
  CHECK(m.composition_readers_known_&&m.composition_reader_known_mask_==0x1fffffu);
  for(DWORD stage:{0u,15u,D3DVERTEXTEXTURESAMPLER0,D3DVERTEXTEXTURESAMPLER3,D3DDMAPSAMPLER}){
   int reader=m.composition_texture_reader(stage,&alias);CHECK(reader==1);m.set_texture(stage,&alias,0,false,reader);CHECK(m.composition_main_sampler_mask_!=0);
   const unsigned queries=alias.identity_queries;CHECK(m.composition_texture_reader(stage,&alias)==2&&alias.identity_queries==queries);
   m.shadow_.recording=true;m.set_texture(stage,nullptr,0,false,0);m.shadow_.recording=false;CHECK(m.composition_main_sampler_mask_!=0);m.set_texture(stage,nullptr,0,false,0);CHECK(m.composition_main_sampler_mask_==0);
  }
  alias.fail_identity=true;CHECK(m.composition_texture_reader(0,&alias)==-1);m.set_texture(0,&alias,0,false,-1);CHECK(!m.composition_readers_known_);m.set_texture(0,nullptr,0,false,0);CHECK(m.composition_readers_known_);
  m.composition_identity_known_=false;CHECK(m.composition_texture_reader(1,&alias)==-1);m.composition_identity_known_=true;
  m.composition_enhanced_=true;m.composition_export();CHECK(m.composition_quarantined_&&m.composition_frame_stopped_&&m.taa_invalidations==1);m.before_reset();CHECK(m.composition_quarantined_&&!m.composition_state_lost_&&m.device_references()==0);
 }
 // Native capability work is done at the latch, once per actual depth
 // format; ordinary draw eligibility only reads cached host state.
 {MotionOutput m;ready(m);auto&p=*m.composition_;m.composition_attach_attempted_=false;m.begin_composition_frame();CHECK(p.attaches==1);
  m.begin_composition_frame();CHECK(p.attaches==1);m.pending_.depth.format=3;m.begin_composition_frame();CHECK(p.attaches==2);
  const unsigned reads=device.texture_reads,samplers=device.sampler_reads,transforms=renderer::emission_transforms;
  m.composition_readers_known_=true;auto route=m.before_draw({});CHECK(route.composition&&device.texture_reads==reads&&device.sampler_reads==samplers&&renderer::emission_transforms==transforms&&p.attaches==2);
 }
 {MotionOutput m;ready(m);auto&p=*m.composition_;m.linear_emission_requested_=false;const unsigned reads=device.texture_reads;m.begin_composition_frame();m.resync_samplers();auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&p.prepares==0&&p.begins==0&&p.attaches==0&&device.texture_reads==reads);}
 // Prebound aliases and failed native getters are classified at resync;
 // returned COM references are released even on a failed getter. Unsupported
 // vertex/displacement capabilities avoid getters and remain known-unbound.
 {MotionOutput m;ready(m);IUnknown identity;IDirect3DBaseTexture9 alias;alias.identity=&identity;m.composition_main_identity_=&identity;
  device.textures[D3DDMAPSAMPLER]=&alias;m.resync_samplers();CHECK(m.composition_main_sampler_mask_==(1u<<20)&&m.composition_readers_known_&&alias.refs==1);
  device.texture_failures[D3DDMAPSAMPLER]=E_FAIL;m.resync_samplers();CHECK(!m.composition_readers_known_&&alias.refs==1);device.texture_failures.clear();device.textures.clear();
  m.caps_.VertexTextureFilterCaps=m.caps_.DevCaps2=0;const unsigned reads=device.texture_reads;m.resync_samplers();CHECK(device.texture_reads==reads+16&&m.composition_readers_known_);
 }
 // Reader uncertainty after enhancement invalidates this frame; sampling a
 // published main surface additionally quarantines future frames.
 for(bool published:{false,true}){MotionOutput m;ready(m);m.composition_effective_=true;m.composition_enhanced_=true;m.composition_readers_known_=false;m.composition_published_=published;
  auto route=m.before_draw({});CHECK(route.submit&&!route.composition&&m.composition_frame_stopped_&&m.composition_->prepares==0);CHECK(m.composition_quarantined_==published&&m.taa_invalidations>=1);
 }
 {MotionOutput m;ready(m);m.lazy_rt1_=true;device.target_result=-72;m.sampler_biased_mask_=1;m.samplers_[0].biased=true;m.samplers_[0].saved_known=true;device.ordinal=0;device.failed_calls={1};CHECK(m.restore_bindings_checked()==E_FAIL);CHECK(!m.lazy_rt1_&&m.sampler_biased_mask_==1&&m.sampler_restore_failed_mask_==1);device.failed_calls.clear();}
 // Capture can call the void restoration wrapper before before_draw. Even
 // after another wrapper consumes the result, motion state loss
 // remains sticky until Reset, independently of optional emission.
 for(bool effective:{false,true})for(unsigned fault=1;fault<3;++fault){MotionOutput m;ready(m);m.composition_effective_=effective;m.linear_emission_requested_=effective;
  if(fault==1){m.sampler_biased_mask_=1;m.samplers_[0].biased=true;m.samplers_[0].saved_known=true;device.ordinal=0;device.failed_calls={1};}
  if(fault==2){m.lazy_rt1_=true;device.target_result=-82;}
  m.restore_bindings();CHECK(m.composition_state_lost_==effective&&m.composition_frame_stopped_==effective);CHECK(m.motion_state_lost_&&m.taa_invalidations==1);
  device.failed_calls.clear();device.target_result=S_OK;m.restore_bindings();CHECK(m.restore_bindings_checked()==S_OK);
  auto route=m.before_draw({});CHECK(!route.submit&&!route.composition);CHECK(m.composition_state_lost_==effective&&m.composition_->prepares==0);
 }
 // A failed flush marks uncertainty immediately, using the actual production
 // function; consuming its error cannot erase that flag.
 {MotionOutput m;ready(m);m.lazy_rt1_=true;device.target_result=-83;CHECK(m.flush_bindings()==-83);CHECK(m.composition_state_lost_&&m.composition_frame_stopped_);
  device.target_result=S_OK;m.restore_bindings();auto route=m.before_draw({});CHECK(!route.submit&&!route.evaluated&&m.composition_->prepares==0);
 }
 {MotionOutput m;ready(m);m.sampler_biased_mask_=1;m.samplers_[0].biased=true;m.samplers_[0].saved_known=true;device.ordinal=0;device.failed_calls={1};CHECK(m.restore_mip_bias()==E_FAIL);CHECK(m.sampler_restore_failed_mask_==1&&m.restore_mip_bias()==S_OK&&m.counters_.mip_bias_restores==1);device.failed_calls.clear();m.restore_bindings();CHECK(m.composition_state_lost_&&!m.before_draw({}).submit);}
 // The writer boundary double records the exact requested main-surface
 // handoff. Query uncertainty requests that handoff, without directly setting
 // quarantine. Getter references are retired inside the busy-accounting guard.
 for(unsigned outcome=0;outcome<7;++outcome){MotionOutput m;ready(m);IDirect3DSurface9 surface;IDirect3DTexture9 main;IUnknown main_identity,other_identity;IDirect3DBaseTexture9 texture;
  m.hdr_main_=&surface;m.composition_main_texture_=&main;m.composition_main_identity_=&main_identity;texture.identity=&other_identity;
  if(outcome==0)texture.fail_identity=true;
  if(outcome==1)texture.identity=nullptr;
  if(outcome==2){texture.fail_identity=true;texture.partial_identity=true;}
  if(outcome==4)texture.identity=&main_identity;
  if(outcome==6)m.composition_identity_known_=false;
  retiring_emission=&m;release_check=writer_retirement_check;m.before_texture_write(outcome==5?&main:&texture);release_check=nullptr;retiring_emission=nullptr;
  const bool written=outcome!=3;CHECK(m.content_writes==(written?1u:0u)&&m.write_target==(written?&surface:nullptr));CHECK(!m.composition_busy_&&!m.composition_quarantined_&&m.composition_counts_.exports==0);
  CHECK(main_identity.refs==1&&other_identity.refs==1&&main.refs==1&&texture.refs==1);CHECK(texture.identity_queries==(outcome==5?0u:1u)&&main.identity_queries==0);
 }
 {MotionOutput m;ready(m);IDirect3DSurface9 surface;IDirect3DBaseTexture9 texture;m.hdr_main_=&surface;m.composition_main_identity_=nullptr;
  m.before_texture_write(nullptr);m.before_texture_write(&texture);CHECK(m.content_writes==0&&texture.identity_queries==0);
 }
 std::printf("linear_emission_route checks=%u\n",checks-before);
}

// Atomic corrected-pair cache: exercise all four aliases and both compiled
// depth modes. These scripted shader tokens qualify control flow only.
MotionOutput* retiring_xt=nullptr;
void xt_retirement_check(){CHECK(retiring_xt&&!retiring_xt->shadow_.xt_default_ready&&!retiring_xt->shadow_.xt_default_pair);CHECK(retiring_xt->shadow_.material_contract.sampler_mask==0);}
void xt_default_cases(){
 const unsigned begin=checks;
 for(bool depth:{false,true})for(DWORD pixel:{22u,23u,24u,25u}){
  Device d;MotionOutput m;m.depth_enabled_=depth;m.configure_linear_materials(true,{});m.device_=&d;
  IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps,other;DWORD vertex=10,negative=20;
  d.bound_vs=&vs;d.bound_ps=&ps;m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);
  m.register_vertex_shader(&vs,&vertex,4,vertex);CHECK(!m.shadow_.xt_default_ready);
  m.register_pixel_shader(&ps,&pixel,4,pixel);CHECK(m.shadow_.xt_default_pair&&m.shadow_.xt_default_ready);
  CHECK(m.device_references()==7&&m.shadow_.material_contract.sampler_mask==29);m.resync_samplers();CHECK(m.linear_material_refusal()==0);
  for(bool material:{false,true}){MotionRoute r;CHECK(m.bind_variant_pair(r,material)==S_OK&&r.linear_material==material);
   CHECK(d.bound_vs==(material?m.shadow_.vs_xt_default_linear:m.shadow_.vs_xt_default_ordinary));
   CHECK(d.bound_ps==(material?m.shadow_.ps_material_variant:m.shadow_.ps_xt_default_ordinary));CHECK(m.undo(r)==S_OK&&d.bound_vs==&vs&&d.bound_ps==&ps);
  }
  // Sampler refusal selects the complete repaired ordinary pair.
  m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,TRUE);CHECK(m.linear_material_refusal()==4);MotionRoute refused;
  CHECK(m.bind_variant_pair(refused,false)==S_OK&&d.bound_vs==m.shadow_.vs_xt_default_ordinary&&d.bound_ps==m.shadow_.ps_xt_default_ordinary);CHECK(m.undo(refused)==S_OK);
  m.set_sampler_state(4,D3DSAMP_SRGBTEXTURE,FALSE);
  // Shared VS retains its previous generic ordinary and material selection.
  auto*ordinary=m.shadow_.vs_variant;auto*linear=m.shadow_.vs_material_variant;
  m.register_pixel_shader(&other,&negative,4,negative);m.set_pixel_shader(&other);CHECK(!m.shadow_.xt_default_pair&&!m.shadow_.xt_default_ready);
  for(bool material:{false,true}){MotionRoute r;CHECK(m.bind_variant_pair(r,material)==S_OK&&d.bound_vs==(material?linear:ordinary));CHECK(m.undo(r)==S_OK);}
  m.set_pixel_shader(&ps);
  // An unfinished registration remains an exact XT identity but cannot
  // degrade to generic shared-VS selection on a subsequent shader setter.
  for(bool vertex_stage:{false,true}){auto&entry=vertex_stage?m.vertex_[&vs]:m.pixel_[&ps];entry.registered=false;
   if(vertex_stage)m.set_vertex_shader(&vs);else m.set_pixel_shader(&ps);
   CHECK(m.shadow_.xt_default_pair&&!m.shadow_.xt_default_ready&&m.shadow_.material_contract.sampler_mask==0);
   entry.registered=true;if(vertex_stage)m.set_vertex_shader(&vs);else m.set_pixel_shader(&ps);CHECK(m.shadow_.xt_default_ready);
  }
  const unsigned lookups=renderer::xt_lookups,contracts=renderer::contract_lookups,transforms=renderer::xt_transforms,creates=d.vs_creates,reads=d.sampler_reads;
  for(unsigned i=0;i<1000;++i)CHECK(m.linear_material_refusal()==0);
  CHECK(renderer::xt_lookups==lookups&&renderer::contract_lookups==contracts&&renderer::xt_transforms==transforms&&d.vs_creates==creates&&d.sampler_reads==reads);
  // Both attempted shader setters mutate before failure. Full rollback then
  // one ordinary repaired attempt retains the first diagnostic failure.
  for(unsigned fail:{1u,2u}){d.ordinal=0;d.calls.clear();d.failed_calls={fail};MotionRoute r;
   CHECK(m.bind_variant_pair(r,true)==S_OK&&!r.linear_material&&r.preparation_error==E_FAIL);
   CHECK(d.bound_vs==m.shadow_.vs_xt_default_ordinary&&d.bound_ps==m.shadow_.ps_xt_default_ordinary);CHECK(d.calls.size()==(fail==1?4u:6u));CHECK(m.undo(r)==S_OK&&d.bound_vs==&vs&&d.bound_ps==&ps);
  }
  // Failure in either rollback slot suppresses any ordinary retry. Restore
  // still attempts the other shader; Reset resync rebuilds cached readiness.
  for(unsigned fail_restore:{3u,4u}){d.ordinal=0;d.calls.clear();d.failed_calls={2,fail_restore};MotionRoute r;
   CHECK(m.bind_variant_pair(r,true)==E_FAIL&&m.motion_state_lost_&&d.calls.size()==4);m.rollback_route(r);CHECK(!r.submit&&r.submission_error==E_FAIL);
   d.failed_calls.clear();d.bound_vs=&vs;d.bound_ps=&ps;m.before_reset();CHECK(!m.shadow_.xt_default_ready);m.after_reset(S_OK);CHECK(m.shadow_.xt_default_ready);
   CHECK(!m.motion_state_lost_);
  }
  // Reentrant release must observe invalidated pair readiness on replacement.
  retiring_xt=&m;release_check=xt_retirement_check;m.register_vertex_shader(&vs,&vertex,4,vertex);release_check=nullptr;CHECK(m.shadow_.xt_default_ready);
  const unsigned held=m.device_references(),released=releases;release_check=xt_retirement_check;m.release_resources();release_check=nullptr;CHECK(releases==released+held&&m.device_references()==0);
 }
 // Failure/partial-object failure in each of the four prerequisites never
 // exposes a half pair. A registration retry is the only creation retry.
 for(DWORD failed:{410u,510u,422u,222u})for(bool partial:{false,true}){
  Device d;MotionOutput m;m.configure_linear_materials(true,{});m.device_=&d;d.fail_program=failed;d.partial_program=partial;
  IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;DWORD v=10,p=22;d.bound_vs=&vs;d.bound_ps=&ps;m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);
  m.register_vertex_shader(&vs,&v,4,v);m.register_pixel_shader(&ps,&p,4,p);
  CHECK(m.shadow_.xt_default_pair&&!m.shadow_.xt_default_ready&&m.shadow_.material_contract.sampler_mask==0&&m.xt_default_unavailable_.seen);
  MotionRoute r;const auto calls=d.calls.size();CHECK(m.bind_variant_pair(r,true)==E_FAIL&&!r.vs_set&&!r.ps_set&&d.calls.size()==calls&&d.bound_vs==&vs&&d.bound_ps==&ps);
  d.fail_program=0;d.partial_program=false;
  if(failed==410||failed==510)m.register_vertex_shader(&vs,&v,4,v);else m.register_pixel_shader(&ps,&p,4,p);
  CHECK(m.shadow_.xt_default_ready&&m.device_references()==7);m.release_resources();
 }
 // XT BUMP's s5 color sample participates in the cached decode gate.
 {Device d;MotionOutput m;m.configure_linear_materials(true,{});m.device_=&d;IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;DWORD v=30,p=44;
  m.register_vertex_shader(&vs,&v,4,v);m.register_pixel_shader(&ps,&p,4,p);m.set_vertex_shader(&vs);m.set_pixel_shader(&ps);m.resync_samplers();CHECK(m.linear_material_refusal()==0);
  m.set_sampler_state(5,D3DSAMP_SRGBTEXTURE,TRUE);CHECK(m.linear_material_refusal()==4);d.fail_sampler=5;m.resync_samplers();CHECK(m.linear_material_refusal()==4);m.release_resources();
 }
 std::printf("linear_material_xt_cache checks=%u\n",checks-begin);
}
// Shared state-write regression: every scripted failure mutates its slot.
// Production target preparation, constant preparation and rollback are reused.
void attempted_state_cases(){
 const unsigned begin=checks;
 for(bool lazy:{false,true})for(unsigned failure:{1u,2u,3u,4u}){
  Device d;MotionOutput m;m.device_=&d;m.lazy_mode_=lazy;m.target_surface_=new IDirect3DSurface9;m.depth_surface_=new IDirect3DSurface9;
  d.mutation_faults=true;d.state_failures={failure};MotionRoute r;r.depth=true;
  CHECK(m.bind_targets(r)==E_FAIL);m.rollback_route(r);
  CHECK(r.submit&&!m.motion_state_lost_&&!d.targets[1]&&!d.targets[2]&&d.write_masks[1]==5&&d.write_masks[2]==6);
  m.release_resources();
 }
 // A failed rollback must suppress the draw; every other attempted slot is
 // still restored, and the first restoration error survives later cleanup.
 for(bool lazy:{false,true}){Device d;MotionOutput m;m.device_=&d;m.lazy_mode_=lazy;m.target_surface_=new IDirect3DSurface9;m.depth_surface_=new IDirect3DSurface9;
  d.mutation_faults=true;d.state_failures={4,5,6};MotionRoute r;r.depth=true;CHECK(m.bind_targets(r)==E_FAIL);m.rollback_route(r);
  CHECK(!r.submit&&m.motion_state_lost_&&r.submission_error==E_FAIL&&d.state_ordinal==8);m.release_resources();
 }
 for(unsigned failure:{1u,2u}){Device d;MotionOutput m;m.device_=&d;d.mutation_faults=true;d.state_failures={failure};
  for(unsigned i=0;i<16;++i)m.shadow_.vs_reserved[i]=d.vs_constants[i]=float(i+1);
  for(unsigned i=0;i<8;++i)m.shadow_.ps_reserved[i]=d.ps_constants[i]=float(i+31);
  m.shadow_.vs_reserved_written=m.shadow_.ps_reserved_written=true;const auto vs=d.vs_constants;const auto ps=d.ps_constants;
  MotionRoute r;CHECK(m.prepare_constants(r)==E_FAIL&&r.vs_constants_set);CHECK(r.ps_constants_set==(failure==2));m.rollback_route(r);
  CHECK(r.submit&&!m.motion_state_lost_&&d.vs_constants==vs&&d.ps_constants==ps&&d.state_ordinal==2*failure);
 }
 // Once a lazy depth target is already held (the depth draw's own mask writes
 // undone, the bindings kept), a mutation-failure of the next row's mask write
 // (1) or of dropping RT2 (2) keeps the pending flags until the flush restores.
 for(unsigned failure:{1u,2u}){Device d;MotionOutput m;m.device_=&d;m.lazy_mode_=true;m.target_surface_=new IDirect3DSurface9;m.depth_surface_=new IDirect3DSurface9;
  MotionRoute depth;depth.depth=true;CHECK(m.bind_targets(depth)==S_OK);CHECK(m.undo(depth)==S_OK&&d.targets[1]&&d.targets[2]&&d.write_masks[1]==5&&d.write_masks[2]==6);d.mutation_faults=true;d.state_failures={failure};MotionRoute ordinary;
  CHECK(m.bind_targets(ordinary)==E_FAIL);m.rollback_route(ordinary);CHECK(ordinary.submit&&!d.targets[1]&&!d.targets[2]&&d.write_masks[1]==5&&d.write_masks[2]==6);m.release_resources();
 }
 // Unavailable/nonrouted pairs must not native-submit if the preceding lazy
 // route cannot restore, including when optional emission is disabled.
 for(bool emission:{false,true}){Device d;MotionOutput m;m.device_=&d;m.composition_effective_=emission;m.lazy_rt1_=true;m.shadow_.xt_default_pair=true;m.shadow_.xt_default_ready=false;
  d.target_result=-91;auto r=m.before_draw({});CHECK(!r.submit&&m.motion_state_lost_&&r.submission_error==-91&&!m.lazy_rt1_);
  CHECK(m.composition_counts_.suppressed==(emission?1u:0u));d.target_result=S_OK;auto next=m.before_draw({});CHECK(!next.submit&&!next.evaluated&&next.submission_error==-91);
 }
 // A void restore point may consume the HRESULT before draw admission;
 // a quiet flush may precede an otherwise routed draw. Both quarantine at
 // the failure itself, before evaluation or any shader/native submission.
 for(bool quiet:{false,true})for(bool emission:{false,true}){Device d;MotionOutput m;m.device_=&d;m.composition_effective_=emission;m.lazy_rt1_=true;m.route_on_evaluation=quiet;
  d.target_result=-92;if(quiet)CHECK(m.flush_bindings()==-92);else m.restore_bindings();
  CHECK(m.motion_state_lost_&&m.motion_state_error_==-92&&m.taa_invalidations>0);CHECK(m.composition_state_lost_==emission);
  const auto shader_calls=d.calls.size();auto r=m.before_draw({});CHECK(!r.submit&&!r.evaluated&&!r.routed&&m.evaluations==0&&d.calls.size()==shader_calls&&r.submission_error==-92);
  // Later cleanup cannot erase or replace the first restoration error.
  d.target_result=-93;m.lazy_rt1_=true;m.restore_bindings();CHECK(m.motion_state_error_==-92);
  d.target_result=S_OK;m.before_reset();m.after_reset(E_FAIL);CHECK(m.motion_state_lost_&&!m.before_draw({}).submit);
  m.recovery_reads_fail=true;m.after_reset(S_OK);CHECK(m.motion_state_lost_&&!m.before_draw({}).submit);
  m.recovery_reads_fail=false;m.after_reset(S_OK);CHECK(!m.motion_state_lost_);auto recovered=m.before_draw({});CHECK(recovered.submit&&recovered.evaluated&&recovered.routed==quiet&&m.evaluations==1);
 }
 // No lazy objects are needed: a consumed mip failure also sticks.
 {Device d;MotionOutput m;m.device_=&d;
  m.sampler_biased_mask_=1;m.samplers_[0].biased=true;m.samplers_[0].saved_known=true;d.failed_calls={1};
  m.restore_bindings();CHECK(m.motion_state_lost_&&m.motion_state_error_==E_FAIL);CHECK(!m.before_draw({}).submit);
 }
 std::printf("motion_attempted_state checks=%u\n",checks-begin);
}
MotionOutput* notice_owner=nullptr;
void reenter_notice(){notice_owner->refresh_linear_material_contract();notice_owner->report_xt_default_unavailable();}
void xt_deferred_notice_cases(){
 const auto before=checks;xt_notice_log={};
 Device d;MotionOutput m;m.device_=&d;
 IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;
 m.shadow_.vs_hash=10;m.shadow_.ps_hash=22;
 m.refresh_linear_material_contract();CHECK(!m.xt_default_unavailable_.seen&&!m.xt_default_unavailable_.pending);
 m.linear_material_requested_=true;m.shadow_.ps_hash=99;m.refresh_linear_material_contract();CHECK(!m.xt_default_unavailable_.seen);
 m.shadow_.ps_hash=22;m.shadow_.vs_registered=m.shadow_.ps_registered=true;
 m.shadow_.vs_xt_default_ordinary=m.shadow_.vs_xt_default_linear=&vs;
 m.shadow_.ps_xt_default_ordinary=m.shadow_.ps_material_variant=&ps;
 m.refresh_linear_material_contract();CHECK(m.shadow_.xt_default_ready&&!m.xt_default_unavailable_.seen);
 m.id_=0x1122334455667788ull;m.shadow_.vs_xt_default_linear=nullptr;m.shadow_.ps_material_variant=nullptr;
 m.refresh_linear_material_contract();CHECK(xt_notice_log.calls==0);
 CHECK(m.xt_default_unavailable_.seen&&m.xt_default_unavailable_.pending&&m.xt_default_unavailable_.ready_mask==5);
 // Later bindings/readiness and even owner ID cannot overwrite the first event.
 m.id_=99;m.shadow_.ps_hash=23;m.shadow_.vs_xt_default_linear=&vs;m.shadow_.ps_material_variant=&ps;
 m.refresh_linear_material_contract();CHECK(m.shadow_.xt_default_ready&&xt_notice_log.calls==0);
 m.shadow_.ps_hash=25;m.shadow_.ps_material_variant=nullptr;m.refresh_linear_material_contract();
 notice_owner=&m;xt_notice_callback=reenter_notice;m.report_xt_default_unavailable();xt_notice_callback=nullptr;notice_owner=nullptr;
 CHECK(xt_notice_log.calls==1&&xt_notice_log.device==0x1122334455667788ull&&xt_notice_log.vs==10&&xt_notice_log.ps==22&&xt_notice_log.ready==5);
 CHECK(!m.xt_default_unavailable_.pending&&m.xt_default_unavailable_.seen);
 m.refresh_linear_material_contract();m.report_xt_default_unavailable();m.release_resources();CHECK(xt_notice_log.calls==1);
 // A separate device can retire before Present: final release drains once.
 MotionOutput retired;retired.device_=&d;retired.linear_material_requested_=true;retired.id_=77;
 retired.shadow_.vs_hash=10;retired.shadow_.ps_hash=24;retired.refresh_linear_material_contract();CHECK(xt_notice_log.calls==1);
 retired.release_resources();CHECK(xt_notice_log.calls==2&&xt_notice_log.device==77&&xt_notice_log.ps==24&&xt_notice_log.ready==0);
 retired.release_resources();retired.report_xt_default_unavailable();CHECK(xt_notice_log.calls==2);
 std::printf("linear_material_xt_deferred_notice checks=%u\n",checks-before);
}
int main(){
 xt_deferred_notice_cases();
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
 CHECK(m.device_references()==6);
 auto original_gain=m.linear_material_config_.direct_gain;m.configure_linear_materials(false,{4,1,1});
 CHECK(m.linear_material_requested_&&m.linear_material_config_.direct_gain==original_gain);
 // Mip bias is zero: six native sampler reads still establish the contract.
 m.resync_samplers();CHECK(device.sampler_reads==6&&m.linear_material_refusal()==0);
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
 for(unsigned failure:{1u,2u}){device.ordinal=0;device.calls.clear();device.failed_calls={failure};route={};CHECK(m.bind_variant_pair(route,true)==S_OK);CHECK(!route.linear_material&&route.jittered&&route.vs_set&&route.ps_set);CHECK(device.bound_vs==m.shadow_.vs_variant&&device.bound_ps==m.shadow_.ps_variant);CHECK(device.calls.size()==(failure==1?4:6));CHECK(m.undo(route)==S_OK);}
 // Failed partial restoration refuses retry, invalidates state and counts it.
 device.ordinal=0;device.calls.clear();device.failed_calls={2,3};route={};CHECK(m.bind_variant_pair(route,true)==E_FAIL);CHECK(device.calls.size()==4&&!route.linear_material&&m.states_invalidated&&m.counters_.restore_failures==1);CHECK(m.motion_state_lost_&&!m.before_draw({}).submit);
 // A later lazy flush failure cannot replace the material undo's error.
 m.lazy_rt1_=true;device.target_result=-71;m.rollback_route(route);device.target_result=S_OK;
 CHECK(!route.submit&&route.submission_error==E_FAIL&&m.motion_state_error_==E_FAIL&&device.calls.size()==4);
 // Conversely, when bindings cleanup fails first, a later shader undo must
 // preserve that earlier HRESULT rather than preferring the later undo error.
 {MotionOutput later;later.device_=&device;later.shadow_.vs=&original_vs;later.lazy_rt1_=true;device.target_result=-72;
  MotionRoute failed;failed.vs_set=true;device.ordinal=0;device.calls.clear();device.failed_calls={1};later.rollback_route(failed);
  CHECK(later.motion_state_lost_&&later.motion_state_error_==-72&&!failed.submit&&failed.submission_error==-72);
  device.target_result=S_OK;CHECK(device.calls.size()==1&&later.counters_.restore_failures==2);CHECK(later.undo(failed)==S_OK&&later.motion_state_error_==-72);
 }

 // The single ordinary retry can itself fail; no recursive material attempt.
 device.ordinal=0;device.calls.clear();device.failed_calls={1,3};route={};CHECK(m.bind_variant_pair(route,true)==E_FAIL);CHECK(device.calls.size()==3&&route.vs_set&&!route.ps_set);device.failed_calls.clear();CHECK(m.undo(route)==S_OK);
 // Failed combined creation preserves ordinary motion, including the bound
 // registry shadow; re-registration releases both old objects exactly once.
 unsigned before=releases;device.fail_combined_create=true;m.register_vertex_shader(&original_vs,&vs,4,10);CHECK(releases==before+4);CHECK(m.shadow_.vs_variant&&!m.shadow_.vs_material_variant&&m.device_references()==3&&m.linear_material_refusal()==2);
 before=releases;m.release_resources();CHECK(releases==before+3&&m.device_references()==0);CHECK(!m.shadow_.vs_variant&&!m.shadow_.vs_material_variant&&!m.shadow_.ps_variant&&!m.shadow_.ps_material_variant);
 before=releases;m.release_resources();CHECK(releases==before);
 // Default off creates only motion and never performs material sampler reads.
 MotionOutput off;Device quiet;off.device_=&quiet;off.register_vertex_shader(&original_vs,&vs,4,10);CHECK(!off.vertex_[&original_vs].material_variant);off.resync_samplers();CHECK(quiet.sampler_reads==0);off.release_resources();
 attempted_state_cases();
 xt_default_cases();
 contract_lifecycle();
 palette_contract_lifecycle();
 emission_cache_cases();
 emission_instance_cache_cases();
 emission_route_cases();
 emission_environment_cases();
 distance_fade_cache_cases();
 distance_fade_route_cases();
 distance_fade_environment_cases();
 std::printf("linear_material_live checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
