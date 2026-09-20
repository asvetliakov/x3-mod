#include "fog_pass.h"
#include "ambient_occlusion_caps.h"
#include "quad_vertex_program.h"
#include "../proxy/cpu_state.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>
namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in AmbientOcclusionPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, UpdateTexture = 31, StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92,
    SetStreamSource = 100, GetStreamSource = 101, SetStreamSourceFreq = 102, GetStreamSourceFreq = 103, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using UpdateTextureFn = HRESULT(WINAPI*)(D, IDirect3DBaseTexture9*, IDirect3DBaseTexture9*);
using StretchRectFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
using SetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using CreateBlockFn = HRESULT(WINAPI*)(D, D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored programs are embedded (tools/shaders/generate_rigid_motion_pixel.py).
constexpr DWORD march_words[] = {
#include "fog_march_program_inc.h"
};
constexpr DWORD composite_words[] = {
#include "fog_composite_program_inc.h"
};
template<class Resource> HRESULT same_device(IDirect3DDevice9* device,Resource* resource) noexcept {
    IDirect3DDevice9* owner=nullptr; HRESULT hr=resource->GetDevice(&owner);
    const bool same=owner==device; drop(owner);
    return FAILED(hr)?hr:(same?S_OK:E_INVALIDARG);
}
bool valid_params(const FogParams& p) noexcept {
    if(!p.world.valid) return false;
    for(float v:{p.m00,p.m11,p.m20,p.m21,p.density_scale,p.anisotropy,p.decode_exponent}) if(!std::isfinite(v)) return false;
    if(p.m00<=0||p.m11<=0||p.density_scale<0||p.density_scale>5||p.anisotropy<0||p.anisotropy>.9f||
       (p.decode_exponent!=1.f&&p.decode_exponent!=2.2f)) return false;
    float length=0;
    for(unsigned i=0;i<3;++i) {
        if(!std::isfinite(p.world.origin_mod[i])||p.world.origin_mod[i]<0||p.world.origin_mod[i]>=fog_volume_period||
           !std::isfinite(p.world.sun_world[i])||!std::isfinite(p.sun_radiance[i])||p.sun_radiance[i]<0||p.sun_radiance[i]>128.f) return false;
        length+=p.world.sun_world[i]*p.world.sun_world[i];
    }
    if(std::abs(length-1.f)>1e-3f) return false;
    for(float v:p.world.inverse_columns) if(!std::isfinite(v)) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        float dot=0;for(unsigned k=0;k<3;++k)dot+=p.world.inverse_columns[3*i+k]*p.world.inverse_columns[3*j+k];
        if(std::abs(dot-(i==j?1.f:0.f))>1e-3f)return false;
    }
    return true;
}
} // namespace
bool fog_valid_params(const FogParams& params) noexcept { return valid_params(params); }
struct FogPass::SavedState {
    const FogPass& pass; IDirect3DSurface9* targets[4]{}; IDirect3DSurface9* depth=nullptr;
    struct Stream { IDirect3DVertexBuffer9* buffer=nullptr; UINT offset=0,stride=0,frequency=0; } streams[16]{};
    D3DVIEWPORT9 viewport{}; RECT scissor{}; bool lost_seen=false;
    explicit SavedState(const FogPass& p):pass(p){}
    ~SavedState(){for(auto& t:targets)drop(t);drop(depth);for(auto& s:streams)drop(s.buffer);}
    HRESULT capture() noexcept {
        ++pass.calls_;HRESULT hr=pass.block_->Capture();if(FAILED(hr))return hr;
        for(UINT i=0;i<pass.render_targets_;++i){hr=pass.call<GetRtFn>(GetRenderTarget)(pass.device_,i,&targets[i]);if(FAILED(hr)&&!(i>0&&hr==D3DERR_NOTFOUND))return hr;}
        hr=pass.call<GetDepthFn>(GetDepthStencilSurface)(pass.device_,&depth);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return hr;
        hr=pass.call<GetViewportFn>(GetViewport)(pass.device_,&viewport);if(FAILED(hr))return hr;
        hr=pass.call<GetScissorFn>(GetScissorRect)(pass.device_,&scissor);if(FAILED(hr))return hr;
        for(UINT i=0;i<pass.streams_;++i){
            hr=pass.call<GetStreamFn>(GetStreamSource)(pass.device_,i,&streams[i].buffer,&streams[i].offset,&streams[i].stride);if(FAILED(hr))return hr;
            hr=pass.call<GetFreqFn>(GetStreamSourceFreq)(pass.device_,i,&streams[i].frequency);if(FAILED(hr))return hr;
        }
        return S_OK;
    }
    HRESULT restore() noexcept {
        HRESULT first=S_OK;
        auto step=[&](HRESULT hr){if(FAILED(hr)&&SUCCEEDED(first))first=hr;if(lost(hr))lost_seen=true;return !lost(hr);};
        D d=pass.device_;
        for(UINT i=0;i<16;++i)if(!step(pass.call<SetTextureFn>(SetTexture)(d,i,nullptr)))return first;
        if(!step(pass.call<SetDepthFn>(SetDepthStencilSurface)(d,nullptr)))return first;
        for(UINT i=1;i<pass.render_targets_;++i)if(!step(pass.call<SetRtFn>(SetRenderTarget)(d,i,nullptr)))return first;
        for(UINT i=0;i<pass.render_targets_;++i)if(!step(pass.call<SetRtFn>(SetRenderTarget)(d,i,targets[i])))return first;
        if(!step(pass.call<SetDepthFn>(SetDepthStencilSurface)(d,depth)))return first;
        ++pass.calls_;if(!step(pass.block_->Apply()))return first;
        // The complete transaction requires these explicit public stream tuples.
        // Isolated ALL-block tests do restore offsets; no blanket API claim is made.
        for(UINT i=0;i<pass.streams_;++i){
            if(!step(pass.call<SetStreamFn>(SetStreamSource)(d,i,streams[i].buffer,streams[i].offset,streams[i].stride)))return first;
            if(!step(pass.call<SetFreqFn>(SetStreamSourceFreq)(d,i,streams[i].frequency)))return first;
        }
        if(!step(pass.call<SetViewportFn>(SetViewport)(d,&viewport)))return first;
        step(pass.call<SetScissorFn>(SetScissorRect)(d,&scissor));return first;
    }
};
FogPass::~FogPass(){detach();}
void FogPass::release_targets() noexcept {
    drop(lit_surface_);drop(scratch_surface_);drop(lit_);drop(scratch_);drop(block_);
    width_=height_=half_width_=half_height_=0;
}
void FogPass::detach() noexcept {
    PreserveCpuState guard;release_targets();drop(atlas_);drop(march_);drop(composite_);drop(quad_vs_);drop(quad_declaration_);
    device_=nullptr;vtable_=nullptr;caps_={};reset_pending_=false;disarm_field();cached_profile_=fog_field::Profile::None;
    field_recipe_=0;base_sigma_=0;std::vector<std::uint16_t>().swap(atlas_bytes_);
    render_targets_=streams_=max_width_=max_height_=0;
}
void FogPass::before_reset() noexcept {PreserveCpuState guard;release_targets();drop(atlas_);disarm_field();reset_pending_=device_!=nullptr;}
void FogPass::after_reset(HRESULT hr) noexcept {PreserveCpuState guard;if(SUCCEEDED(hr))reset_pending_=false;}
unsigned FogPass::references() const noexcept {
    unsigned n=0;
    for(const void* p:{static_cast<void*>(atlas_),static_cast<void*>(lit_),static_cast<void*>(scratch_),static_cast<void*>(lit_surface_),static_cast<void*>(scratch_surface_),static_cast<void*>(march_),static_cast<void*>(composite_),static_cast<void*>(quad_vs_),static_cast<void*>(quad_declaration_),static_cast<void*>(block_)})n+=p!=nullptr;
    return n;
}
HRESULT FogPass::attach(D d,void* const* native,const D3DCAPS9& caps,D3DFORMAT format) noexcept {
    PreserveCpuState guard;detach();
    auto refuse=[&](const char* reason,HRESULT hr=D3DERR_NOTAVAILABLE){device_=nullptr;vtable_=nullptr;caps_.reason=reason;return hr;};
    if(!d||!native)return refuse("device_native_table",E_INVALIDARG);
    if(caps.PixelShaderVersion<D3DPS_VERSION(3,0)||caps.VertexShaderVersion<D3DVS_VERSION(3,0))return refuse("shader_model3");
    for(auto p:{std::pair{march_words,std::size(march_words)},std::pair{composite_words,std::size(composite_words)}}){
        const unsigned slots=ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(p.first),p.second);
        if(!slots||slots>caps.MaxPixelShader30InstructionSlots)return refuse("compiled_slots");
        caps_.largest_program_slots=std::max(caps_.largest_program_slots,slots);
    }
    if(caps.TextureCaps&(D3DPTEXTURECAPS_POW2|D3DPTEXTURECAPS_NONPOW2CONDITIONAL|D3DPTEXTURECAPS_SQUAREONLY))return refuse("unrestricted_npot2d");
    if(caps.MaxTextureWidth<1560||caps.MaxTextureHeight<1430||caps.NumSimultaneousRTs<1||caps.NumSimultaneousRTs>4||caps.MaxStreams<1||caps.MaxStreams>16||caps.MaxVertexShaderConst<8)return refuse("dimensions_streams_constants");
    constexpr DWORD filters=D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFLINEAR;
    if((caps.TextureFilterCaps&filters)!=filters)return refuse("fp16_linear_filter_caps");
    if(!(caps.DevCaps2&D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES))return refuse("stretchrect_texture");
    device_=d;vtable_=native;IDirect3D9* api=nullptr;D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr=call<GetD3DFn>(GetDirect3D)(d,&api);
    if(SUCCEEDED(hr)&&!api)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=call<GetCreationFn>(GetCreationParameters)(d,&creation);
    const char* reason="format_query";
    if(SUCCEEDED(hr)){
        struct Query{DWORD usage;D3DFORMAT format;const char* name;};
        for(auto q:{Query{0,D3DFMT_A32B32G32R32F,"rgba32f_texture"},Query{D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,"fp16_rt"},Query{D3DUSAGE_QUERY_FILTER,D3DFMT_A16B16G16R16F,"fp16_filter_query"}}){
            hr=api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,format,q.usage,D3DRTYPE_TEXTURE,q.format);
            if(hr!=D3D_OK){reason=q.name;hr=D3DERR_NOTAVAILABLE;break;}
        }
    }
    drop(api);caps_.formats=hr;if(FAILED(hr))return refuse(reason,hr);
    render_targets_=caps.NumSimultaneousRTs;streams_=caps.MaxStreams;max_width_=caps.MaxTextureWidth;max_height_=caps.MaxTextureHeight;
    hr=call<CreateVsFn>(CreateVertexShader)(d,reinterpret_cast<const DWORD*>(quad_vertex_program()),&quad_vs_);
    if(SUCCEEDED(hr))hr=call<CreateDeclarationFn>(CreateVertexDeclaration)(d,quad_declaration,&quad_declaration_);
    if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(d,march_words,&march_);
    if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(d,composite_words,&composite_);
    if(FAILED(hr)){const unsigned slots=caps_.largest_program_slots;detach();caps_.largest_program_slots=slots;caps_.programs=hr;return refuse("program_create",hr);}
    caps_.programs=hr;caps_.enabled=true;caps_.reason="";return S_OK;
}
HRESULT FogPass::prepare_field(void* module,fog_field::Profile profile) noexcept {
    PreserveCpuState guard;
    if(profile==fog_field::Profile::None){disarm_field();return S_FALSE;}
    if(!device_||!caps_.enabled){disarm_field();return E_INVALIDARG;}
    if(reset_pending_){disarm_field();return D3DERR_DEVICENOTRESET;}
    if(profile==active_profile_&&atlas_)return S_OK;
    disarm_field();
    const auto* info=fog_field::profile_info(profile);if(!info)return E_INVALIDARG;
    if(cached_profile_!=profile){
        drop(atlas_);cached_profile_=fog_field::Profile::None;field_recipe_=0;base_sigma_=0;
        const auto decoded=fog_field::decode_from_resource(module,profile,atlas_bytes_);
        if(!decoded)return static_cast<HRESULT>(decoded.hresult);
        cached_profile_=profile;field_recipe_=info->recipe_id;base_sigma_=info->base_sigma;
    }
    if(!atlas_){
        IDirect3DTexture9* upload=nullptr;
        HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,info->width,info->height,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&upload,nullptr);
        if(SUCCEEDED(hr)){
            D3DLOCKED_RECT lock{};hr=upload->LockRect(0,&lock,nullptr,0);
            if(SUCCEEDED(hr)){
                if(!lock.pBits||lock.Pitch<static_cast<INT>(info->width*8u))hr=E_FAIL;
                else for(UINT y=0;y<info->height;++y)std::memcpy(static_cast<char*>(lock.pBits)+static_cast<std::size_t>(y)*lock.Pitch,atlas_bytes_.data()+static_cast<std::size_t>(y)*info->width*4,info->width*8u);
                const HRESULT unlock=upload->UnlockRect(0);if(SUCCEEDED(hr)||lost(unlock))hr=unlock;
            }
        }
        if(SUCCEEDED(hr))hr=call<CreateTextureFn>(CreateTexture)(device_,info->width,info->height,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&atlas_,nullptr);
        if(SUCCEEDED(hr))hr=call<UpdateTextureFn>(UpdateTexture)(device_,upload,atlas_);
        drop(upload);
        if(FAILED(hr)){drop(atlas_);if(lost(hr))reset_pending_=true;return hr;}
        ++allocations_;
    }
    active_profile_=profile;++field_generation_;return S_OK;
}
HRESULT FogPass::prepare_targets(UINT w,UINT h) noexcept {
    PreserveCpuState guard;
    if(!device_||!caps_.enabled)return E_INVALIDARG;
    if(reset_pending_)return D3DERR_DEVICENOTRESET;
    if(!w||!h||w>max_width_||h>max_height_)return E_INVALIDARG;
    if(width_==w&&height_==h&&block_)return S_OK;
    release_targets();
    auto create=[&](UINT x,UINT y,IDirect3DTexture9*& texture,IDirect3DSurface9*& surface){
        HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,x,y,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&texture,nullptr);
        if(SUCCEEDED(hr))hr=texture->GetSurfaceLevel(0,&surface);
        return hr;
    };
    HRESULT hr=create((w+1)/2,(h+1)/2,lit_,lit_surface_);
    if(SUCCEEDED(hr))hr=create(w,h,scratch_,scratch_surface_);
    if(SUCCEEDED(hr))hr=call<CreateBlockFn>(CreateStateBlock)(device_,D3DSBT_ALL,&block_);
    if(FAILED(hr)){release_targets();if(lost(hr))reset_pending_=true;return hr;}
    width_=w;height_=h;half_width_=(w+1)/2;half_height_=(h+1)/2;++allocations_;return S_OK;
}
HRESULT FogPass::normalize() noexcept{
#define STEP(expr) do{HRESULT hr=(expr);if(FAILED(hr))return hr;}while(false)
    for(UINT i=0;i<16;++i)STEP(call<SetTextureFn>(SetTexture)(device_,i,nullptr));
    for(UINT i=0;i<4;++i)STEP(call<SetTextureFn>(SetTexture)(device_,D3DVERTEXTEXTURESAMPLER0+i,nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(device_,nullptr));
    for(UINT i=1;i<render_targets_;++i)STEP(call<SetRtFn>(SetRenderTarget)(device_,i,nullptr));
    STEP(call<SetVsFn>(SetVertexShader)(device_,quad_vs_));STEP(call<SetDeclarationFn>(SetVertexDeclaration)(device_,quad_declaration_));STEP(call<SetIndicesFn>(SetIndices)(device_,nullptr));
    for(UINT i=0;i<streams_;++i){STEP(call<SetFreqFn>(SetStreamSourceFreq)(device_,i,1));STEP(call<SetStreamFn>(SetStreamSource)(device_,i,nullptr,0,0));}
    for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})STEP(call<SetRsFn>(SetRenderState)(device_,s,FALSE));
    STEP(call<SetRsFn>(SetRenderState)(device_,D3DRS_VERTEXBLEND,D3DVBF_DISABLE));STEP(call<SetRsFn>(SetRenderState)(device_,D3DRS_FILLMODE,D3DFILL_SOLID));STEP(call<SetRsFn>(SetRenderState)(device_,D3DRS_CULLMODE,D3DCULL_NONE));STEP(call<SetRsFn>(SetRenderState)(device_,D3DRS_COLORWRITEENABLE,15));STEP(call<SetRsFn>(SetRenderState)(device_,D3DRS_MULTISAMPLEMASK,0xffffffff));
    for(UINT i=0;i<8;++i)STEP(call<SetRsFn>(SetRenderState)(device_,D3DRENDERSTATETYPE(D3DRS_WRAP0+i),0));
    for(UINT i=0;i<4;++i){
        STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MINFILTER,i==1?D3DTEXF_LINEAR:D3DTEXF_POINT));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MAGFILTER,i==1?D3DTEXF_LINEAR:D3DTEXF_POINT));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MIPMAPLODBIAS,0));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MAXMIPLEVEL,0));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MAXANISOTROPY,1));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_SRGBTEXTURE,FALSE));
    }
#undef STEP
    return S_OK;
}
HRESULT FogPass::bind_target(IDirect3DSurface9* s,UINT w,UINT h,IDirect3DPixelShader9* ps) noexcept{
    HRESULT hr=S_OK;
    for(UINT i=0;i<4&&SUCCEEDED(hr);++i){hr=call<SetTextureFn>(SetTexture)(device_,i,nullptr);}
    if(SUCCEEDED(hr)){hr=call<SetRtFn>(SetRenderTarget)(device_,0,s);}
    D3DVIEWPORT9 vp{0,0,w,h,0,1};
    if(SUCCEEDED(hr)){hr=call<SetViewportFn>(SetViewport)(device_,&vp);}
    if(SUCCEEDED(hr)){hr=call<SetPsFn>(SetPixelShader)(device_,ps);}return hr;
}
HRESULT FogPass::quad(UINT w,UINT h) noexcept{x3m::renderer::QuadVertex q[4];x3m::renderer::quad_vertices(w,h,q);return call<DrawUpFn>(DrawPrimitiveUP)(device_,D3DPT_TRIANGLESTRIP,2,q,sizeof(q[0]));}
HRESULT FogPass::execute(const FogFrame& f,FogResult* output) noexcept {
    PreserveCpuState guard;FogResult r{};r.scene_known=f.caller_scene_known;r.scene_open=f.caller_scene_open;r.caller_state_restored=f.caller_scene_known;
    const unsigned initial_calls=calls_;
    auto finish=[&](HRESULT hr){r.device_calls=calls_-initial_calls;if(output)*output=r;return hr;};
    auto refuse=[&](HRESULT hr){r.operation=hr;r.failed=FogStage::Validate;if(lost(hr)){reset_pending_=true;r.scene_known=false;r.caller_state_restored=false;r.route_poisoned=true;}return finish(hr);};
    if(!device_||!caps_.enabled||!f.depth_share||!f.target||!f.main_target||!f.linear_depth_current||!f.caller_scene_known||
       f.caller_stateblock_recording||!f.caller_queries_idle||!fog_valid_params(f.params))return refuse(E_INVALIDARG);
    if(reset_pending_)return refuse(D3DERR_DEVICENOTRESET);
    // Off is a strict zero-device-call path, including no resource validation.
    if(f.params.density_scale==0){r.operation=S_FALSE;return finish(S_FALSE);}
    if(!resources_ready(f.width,f.height,f.profile,f.recipe_id,f.field_generation))return refuse(E_INVALIDARG);
    if(f.depth_share==atlas_||f.depth_share==lit_||f.depth_share==scratch_||f.target==lit_surface_||f.target==scratch_surface_)return refuse(E_INVALIDARG);
    D3DSURFACE_DESC ds{},ss{};HRESULT hr=f.depth_share->GetLevelDesc(0,&ds);if(FAILED(hr))return refuse(hr);
    hr=f.target->GetDesc(&ss);if(FAILED(hr))return refuse(hr);
    if(ds.Width!=width_||ds.Height!=height_||ds.Format!=D3DFMT_A32B32G32R32F||ss.Width!=width_||ss.Height!=height_||ss.Format!=D3DFMT_A16B16G16R16F||
       ss.MultiSampleType!=D3DMULTISAMPLE_NONE||!(ss.Usage&D3DUSAGE_RENDERTARGET))return refuse(E_INVALIDARG);
    hr=same_device(device_,f.depth_share);if(FAILED(hr))return refuse(hr);
    hr=same_device(device_,f.target);if(FAILED(hr))return refuse(hr);
    float constants[9][4]{};
    const auto& p=f.params;
    constants[0][0]=p.m00;constants[0][1]=p.m11;constants[0][2]=p.m20;constants[0][3]=p.m21;
    constants[1][0]=static_cast<float>(width_);constants[1][1]=static_cast<float>(height_);constants[1][2]=static_cast<float>(half_width_);constants[1][3]=static_cast<float>(half_height_);
    for(unsigned i=0;i<3;++i){constants[2][i]=p.world.origin_mod[i];constants[3][i]=p.world.sun_world[i];}
    constants[2][3]=base_sigma_*p.density_scale;constants[3][3]=fog_volume_horizon;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)constants[4+i][j]=p.world.inverse_columns[3*i+j];
    fog_phase_constants(p.anisotropy,p.decode_exponent,p.sun_radiance,constants[7],constants[8]);
    SavedState saved(*this);r.failed=FogStage::Capture;hr=saved.capture();
    if(FAILED(hr)){r.operation=hr;if(lost(hr)){reset_pending_=true;r.scene_known=false;r.caller_state_restored=false;r.route_poisoned=true;}return finish(hr);}
    bool lost_seen=false,changed=false,opened_here=false,closed_borrowed=false;
    r.operation=S_OK;
    auto record=[&](FogStage stage,HRESULT value){
        if(FAILED(value)&&SUCCEEDED(r.operation)){r.operation=value;r.failed=stage;}
        if(lost(value)){lost_seen=true;r.scene_known=false;r.route_poisoned=true;}
        return SUCCEEDED(value);
    };
    if(f.caller_scene_open){
        hr=call<SceneFn>(EndScene)(device_);
        if(record(FogStage::CloseScene,hr)){r.scene_open=false;closed_borrowed=true;}
        else {r.scene_known=false;r.route_poisoned=true;}
    }
    if(SUCCEEDED(r.operation)){
        changed=true;record(FogStage::Normalize,normalize());
    }
    if(SUCCEEDED(r.operation))record(FogStage::Copy,call<StretchRectFn>(StretchRect)(device_,f.target,nullptr,scratch_surface_,nullptr,D3DTEXF_NONE));
    // A borrowed scene must be reopened even if normalization/copy failed. Once
    // reopening fails, recovery repairs only scene ownership: draws stay forbidden.
    bool may_draw=SUCCEEDED(r.operation);
    if(!lost_seen&&(closed_borrowed||(!f.caller_scene_open&&may_draw))){
        hr=call<SceneFn>(BeginScene)(device_);
        if(SUCCEEDED(hr)){r.scene_known=true;r.scene_open=true;opened_here=!f.caller_scene_open;}
        else {
            record(f.caller_scene_open?FogStage::ReopenScene:FogStage::Scene,hr);may_draw=false;r.scene_known=false;
            if(closed_borrowed&&!lost_seen){
                r.scene_recovery=call<SceneFn>(BeginScene)(device_);
                if(SUCCEEDED(r.scene_recovery)){r.scene_known=true;r.scene_open=true;}
                else record(FogStage::RecoverScene,r.scene_recovery);
            }
            if(!r.scene_known)r.route_poisoned=true;
        }
    }
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,bind_target(lit_surface_,half_width_,half_height_,march_));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetPsConstantsFn>(SetPixelShaderConstantF)(device_,0,&constants[0][0],9));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,0,f.depth_share));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,1,atlas_));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,quad(half_width_,half_height_));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::Composite,bind_target(f.target,width_,height_,composite_));
    if(may_draw&&SUCCEEDED(r.operation)){
        IDirect3DTexture9* textures[]={f.depth_share,atlas_,scratch_,lit_};
        for(UINT i=0;i<4&&SUCCEEDED(r.operation);++i)record(FogStage::Composite,call<SetTextureFn>(SetTexture)(device_,i,textures[i]));
        if(SUCCEEDED(r.operation)){r.scene_write_started=true;r.applied=record(FogStage::Composite,quad(width_,height_));}
    }
    if(opened_here&&!lost_seen){
        hr=call<SceneFn>(EndScene)(device_);
        if(record(FogStage::EndScene,hr)){r.scene_known=true;r.scene_open=false;}
        else {r.scene_known=false;r.route_poisoned=true;}
    }
    // An initial failed close changed no bindings and permits no further device
    // work. Lost-device cleanup also releases references without unsafe restores.
    if(lost_seen)r.restore=D3DERR_DEVICELOST;
    else if(changed)r.restore=saved.restore();
    else r.restore=S_OK;
    if(saved.lost_seen||lost(r.restore)){lost_seen=true;r.scene_known=false;}
    if(FAILED(r.restore)){r.route_poisoned=true;if(SUCCEEDED(r.operation))r.failed=FogStage::Restore;}
    if(lost_seen)reset_pending_=true;
    r.caller_state_restored=SUCCEEDED(r.restore)&&r.scene_known&&r.scene_open==f.caller_scene_open;
    if(!r.caller_state_restored)r.route_poisoned=true;
    if(SUCCEEDED(r.operation)&&SUCCEEDED(r.restore)&&r.caller_state_restored){r.failed=FogStage::None;r.lit=lit_;r.half_width=half_width_;r.half_height=half_height_;}
    return finish(FAILED(r.operation)?r.operation:r.restore);
}
} // namespace x3m::renderer
