#include "fog_pass.h"
#include "../fog/fog_density_cache.h"
#include "ambient_occlusion_caps.h"
#include "quad_vertex_program.h"
#include "../proxy/cpu_state.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <new>
#include <utility>
namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in AmbientOcclusionPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, UpdateSurface = 30, UpdateTexture = 31, StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
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
using UpdateSurfaceFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT*);
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
// Stored-density programs (src/fog/fog_density_*_ps.hlsl). Repair is 510 of 512 ps_3_0 slots
// on the Microsoft table: nothing may be added to it (fog_density_shader_slots.py).
constexpr DWORD density_march_words[] = {
#include "fog_density_march_program_inc.h"
};
constexpr DWORD density_composite_words[] = {
#include "fog_density_composite_program_inc.h"
};
constexpr DWORD density_repair_words[] = {
#include "fog_density_repair_program_inc.h"
};
constexpr unsigned density_required_slots=512;
// Look presets L1-L3 (FOG_LOOK variants, each inside the same 512 ps_3_0 slots as the base programs;
// fog_density_shader_slots.py). A creation failure keeps the base path and every frame draws L0.
constexpr DWORD look_march1_words[] = {
#include "fog_density_march_look1_program_inc.h"
};
constexpr DWORD look_march2_words[] = {
#include "fog_density_march_look2_program_inc.h"
};
constexpr DWORD look_composite_words[] = {
#include "fog_density_composite_look_program_inc.h"
};
constexpr DWORD look_repair1_words[] = {
#include "fog_density_repair_look1_program_inc.h"
};
constexpr DWORD look_repair2_words[] = {
#include "fog_density_repair_look2_program_inc.h"
};
// c0.zw of the full-resolution repair draw. FogParams::m20/m21 carry the raster offset plus
// the full-resolution quad pixel-centre term (+1/W, -1/H): a program that forms
// ndc = 2 uv - 1 from the centre uv of full texel P then looks through raster pixel P, the
// point RT2 texel P was rasterised at. The repair draw shades texel P at uv (P+.5)/W, and the
// half-resolution march re-derives uv (2p+.5)/W of the texel it taps, so both draws take this
// same full-resolution term; a half-resolution quad term (+1/hw) would put the repair ray
// half a pixel beside its depth tap. The fixture checks repaired pixels against a CPU march
// through raster pixel P (fog_density_pass_fixture.cpp, repair_matches_cpu_reference).
inline void density_repair_projection(const FogParams& p,float c0[4]) noexcept { c0[0]=p.m00;c0[1]=p.m11;c0[2]=p.m20;c0[3]=p.m21; }
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
void FogPass::release_density_default() noexcept {
    for(unsigned i=0;i<2;++i){drop(density_atlas_surface_[i]);drop(density_atlas_[i]);}
    if(density_)density_->gpu_reset();
    density_status_.ready_fine=density_status_.ready_far=0;
}
void FogPass::abandon_density_worker() noexcept { if(density_){density_->abandon();density_=nullptr;} }
void FogPass::invalidate_density() noexcept { PreserveCpuState guard;if(density_)density_->invalidate();density_status_.ready_fine=density_status_.ready_far=0; }
bool FogPass::density_drawable(const double camera[3]) const noexcept {
    return density_&&camera&&density_status_.available&&density_status_.ready_far>0&&density_->covers(1,camera);
}
bool FogPass::field_family(float chroma[3],float* sigma) const noexcept {
    // Offline constants (fog_family_chroma_inc.h, pinned packets): no scan of the decoded atlas here.
    struct Row{std::uint32_t profile;float chroma[3];};
    static constexpr Row rows[]={
#include "fog_family_chroma_inc.h"
    };
    if(cached_profile_==fog_field::Profile::None||!chroma||!sigma||!(base_sigma_>0))return false;
    for(const auto& row:rows)if(row.profile==static_cast<std::uint32_t>(cached_profile_)){
        for(unsigned c=0;c<3;++c)chroma[c]=row.chroma[c];
        *sigma=base_sigma_;return true;
    }
    return false;
}
void FogPass::detach() noexcept {
    PreserveCpuState guard;release_targets();drop(atlas_);
    release_density_default();
    for(unsigned i=0;i<2;++i){drop(density_staging_surface_[i]);drop(density_staging_[i]);}
    drop(density_march_);drop(density_composite_);drop(density_repair_);
    for(unsigned i=0;i<2;++i){drop(look_march_[i]);drop(look_repair_[i]);}drop(look_composite_);
    fog::DensityCache::retire(density_);density_=nullptr; // joins the worker; a cache it had to abandon is leaked, not freed
    density_config_={};density_status_={};density_refused_=false;ps30_slots_=0;drop(march_);drop(composite_);drop(quad_vs_);drop(quad_declaration_);
    device_=nullptr;vtable_=nullptr;caps_={};reset_pending_=false;disarm_field();cached_profile_=fog_field::Profile::None;
    field_recipe_=0;base_sigma_=0;std::vector<std::uint16_t>().swap(atlas_bytes_);
    render_targets_=streams_=max_width_=max_height_=0;
}
// Reset keeps the worker, both CPU caches and the SYSTEMMEM staging textures; only the DEFAULT
// atlases go, and the cache re-uploads every committed tile under the normal budget afterwards.
void FogPass::before_reset() noexcept {PreserveCpuState guard;release_targets();drop(atlas_);release_density_default();disarm_field();reset_pending_=device_!=nullptr;}
void FogPass::after_reset(HRESULT hr) noexcept {PreserveCpuState guard;if(SUCCEEDED(hr))reset_pending_=false;}
unsigned FogPass::references() const noexcept {
    unsigned n=0;
    for(unsigned i=0;i<2;++i)n+=(density_staging_[i]!=nullptr)+(density_atlas_[i]!=nullptr)+(density_staging_surface_[i]!=nullptr)+(density_atlas_surface_[i]!=nullptr);
    n+=(density_march_!=nullptr)+(density_composite_!=nullptr)+(density_repair_!=nullptr);
    for(unsigned i=0;i<2;++i)n+=(look_march_[i]!=nullptr)+(look_repair_[i]!=nullptr);
    n+=look_composite_!=nullptr;
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
    ps30_slots_=caps.MaxPixelShader30InstructionSlots;
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
HRESULT FogPass::density_resources() noexcept {
    auto refuse=[&](const char* reason,HRESULT hr){density_refused_=true;density_status_.available=false;density_status_.reason=reason;return hr;};
    if(!density_march_){
        // attach already proved ps_3_0, unrestricted NPOT >= 1560x1430, FP16 linear filtering and FP16 targets.
        if(ps30_slots_<density_required_slots)return refuse("density_ps30_slots",D3DERR_NOTAVAILABLE);
        for(auto p:{std::pair{density_march_words,std::size(density_march_words)},std::pair{density_composite_words,std::size(density_composite_words)},std::pair{density_repair_words,std::size(density_repair_words)}})
            if(!ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(p.first),p.second))return refuse("density_compiled_slots",D3DERR_NOTAVAILABLE);
        HRESULT hr=call<CreatePsFn>(CreatePixelShader)(device_,density_march_words,&density_march_);
        if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,density_composite_words,&density_composite_);
        if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,density_repair_words,&density_repair_);
        if(FAILED(hr)){drop(density_march_);drop(density_composite_);drop(density_repair_);if(lost(hr)){reset_pending_=true;return hr;}return refuse("density_program_create",hr);}
        // The look variants are created here, once, never on the hotkey or draw path. Pixel shaders survive
        // Reset. Any refusal keeps the base path and leaves every look drawing L0.
        density_status_.looks=false;
        {
            struct {const DWORD* words;std::size_t count;IDirect3DPixelShader9** out;} const looks[]={{look_march1_words,std::size(look_march1_words),&look_march_[0]},{look_march2_words,std::size(look_march2_words),&look_march_[1]},
                {look_composite_words,std::size(look_composite_words),&look_composite_},{look_repair1_words,std::size(look_repair1_words),&look_repair_[0]},{look_repair2_words,std::size(look_repair2_words),&look_repair_[1]}};
            for(const auto& l:looks){
                const unsigned slots=ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(l.words),l.count);
                hr=slots&&slots<density_required_slots?call<CreatePsFn>(CreatePixelShader)(device_,l.words,l.out):E_FAIL;
                if(FAILED(hr))break;
            }
            if(FAILED(hr)){
                for(unsigned i=0;i<2;++i){drop(look_march_[i]);drop(look_repair_[i]);}drop(look_composite_);
                if(lost(hr)){drop(density_march_);drop(density_composite_);drop(density_repair_);reset_pending_=true;return hr;}
                density_status_.look_reason="program_create";
            } else {density_status_.looks=true;density_status_.look_reason="";}
        }
    }
    if(!density_){
        density_=new(std::nothrow) fog::DensityCache;
        if(!density_||!density_->start()){fog::DensityCache::retire(density_);density_=nullptr;return refuse("density_worker",E_OUTOFMEMORY);}
    }
    for(unsigned i=0;i<2;++i){
        if(!density_staging_[i]){
            HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,fog::kAtlasWidth,fog::kAtlasHeight,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&density_staging_[i],nullptr);
            if(SUCCEEDED(hr))hr=density_staging_[i]->GetSurfaceLevel(0,&density_staging_surface_[i]);
            if(FAILED(hr)){drop(density_staging_surface_[i]);drop(density_staging_[i]);if(lost(hr)){reset_pending_=true;return hr;}return refuse("density_staging",hr);}
            ++allocations_;
        }
        if(!density_atlas_[i]){
            HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,fog::kAtlasWidth,fog::kAtlasHeight,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&density_atlas_[i],nullptr);
            if(SUCCEEDED(hr))hr=density_atlas_[i]->GetSurfaceLevel(0,&density_atlas_surface_[i]);
            if(FAILED(hr)){drop(density_atlas_surface_[i]);drop(density_atlas_[i]);if(lost(hr)){reset_pending_=true;return hr;}return refuse("density_atlas",hr);}
            // A new DEFAULT atlas holds nothing: every committed tile is uploaded again.
            density_->gpu_reset();++allocations_;
        }
    }
    return S_OK;
}
HRESULT FogPass::density_uploads(unsigned budget) noexcept {
    density_status_.upload_bytes=density_status_.upload_rects=0;
    if(!density_->has_work())return S_OK; // steady state: three atomic loads, no lock, no device call
    fog::StagingView views[fog::kLevelCount]{};HRESULT hr=S_OK;
    for(int i=0;i<fog::kLevelCount&&SUCCEEDED(hr);++i){
        if(!density_->level_dirty(i))continue;
        // UpdateSurface takes an explicit source rectangle, so the texture's dirty region is not used.
        D3DLOCKED_RECT lock{};hr=density_staging_[i]->LockRect(0,&lock,nullptr,D3DLOCK_NO_DIRTY_UPDATE);
        if(SUCCEEDED(hr)&&(!lock.pBits||lock.Pitch<INT(fog::kAtlasPitch))){density_staging_[i]->UnlockRect(0);hr=E_FAIL;}
        if(SUCCEEDED(hr))views[i]={static_cast<std::uint8_t*>(lock.pBits),std::size_t(lock.Pitch)};
    }
    fog::TileRect rects[fog::kDefaultUploadRects];unsigned count=0; // bounds the UpdateSurface calls of one frame
    if(SUCCEEDED(hr))count=density_->take_uploads(views,budget,rects,unsigned(std::size(rects)));
    for(int i=0;i<fog::kLevelCount;++i)if(views[i].bits){const HRESULT unlock=density_staging_[i]->UnlockRect(0);if(SUCCEEDED(hr))hr=unlock;}
    for(unsigned i=0;i<count&&SUCCEEDED(hr);++i){
        const fog::TileRect& t=rects[i];const RECT source{t.x,t.y,t.x+t.width,t.y+t.height};const POINT at{t.x,t.y};
        hr=call<UpdateSurfaceFn>(UpdateSurface)(device_,density_staging_surface_[t.level],&source,density_atlas_surface_[t.level],&at);
        if(SUCCEEDED(hr)){density_status_.upload_bytes+=unsigned(t.bytes());++density_status_.upload_rects;}
    }
    // A failure re-queues every committed tile; nothing handed out above counts as resident.
    density_->confirm_uploads(SUCCEEDED(hr));
    density_status_.upload_bytes_total+=density_status_.upload_bytes;density_status_.upload_rects_total+=density_status_.upload_rects;
    return hr;
}
HRESULT FogPass::prepare_density(const FogDensityConfig& config,const double camera[3],std::uint64_t frame) noexcept {
    if(!config.enabled){ // off: no CPU-state capture, no allocation, no device call
        if(density_status_.available||density_status_.ready_far!=0){density_status_.available=false;density_status_.reason="off";density_status_.ready_fine=density_status_.ready_far=0;}
        return S_FALSE;
    }
    PreserveCpuState guard;
    // Every failure below leaves the path unavailable for this frame: execute refuses a density frame.
    density_status_.available=false;density_status_.ready_fine=density_status_.ready_far=0;
    if(!device_||!caps_.enabled||!camera)return E_INVALIDARG;
    if(density_refused_)return D3DERR_NOTAVAILABLE;
    if(reset_pending_)return D3DERR_DEVICENOTRESET;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(camera[i])||!std::isfinite(config.world_offset[i])||!std::isfinite(config.chroma[i])||config.chroma[i]<0||config.chroma[i]>16.f)return E_INVALIDARG;
    if(!std::isfinite(config.sigma)||config.sigma<=0||config.sigma>1.f)return E_INVALIDARG;
    HRESULT hr=density_resources();if(FAILED(hr))return hr;
    density_config_=config;
    fog::CacheIdentity identity;identity.sector_key=config.sector_key;identity.recipe=config.recipe;identity.offset={config.world_offset[0],config.world_offset[1],config.world_offset[2]};
    density_->configure(identity);
    const fog::FrameState state=density_->step(camera,frame);
    hr=density_uploads(config.upload_budget_bytes?config.upload_budget_bytes:unsigned(fog::kDefaultUploadBudget));
    if(FAILED(hr)){if(lost(hr))reset_pending_=true;return hr;}
    density_status_.available=true;density_status_.reason="";density_status_.ready_fine=state.ready[0];density_status_.ready_far=state.ready[1];
    const fog::CacheStats stats=density_->stats();
    density_status_.nodes_generated=stats.nodes_generated;density_status_.worker_busy_us=stats.worker_busy_us;density_status_.missed_locks=stats.missed_locks;
    return S_OK;
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
HRESULT FogPass::normalize(bool density) noexcept{
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
    for(UINT i=0;i<(density?8u:7u);++i){
        const bool linear=i==1||i==7; // s1 atlas (fine), s7 far atlas
        STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MINFILTER,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MAGFILTER,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MIPMAPLODBIAS,0));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MAXMIPLEVEL,0));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_MAXANISOTROPY,1));STEP(call<SetSamplerFn>(SetSamplerState)(device_,i,D3DSAMP_SRGBTEXTURE,FALSE));
    }
#undef STEP
    return S_OK;
}
HRESULT FogPass::bind_target(IDirect3DSurface9* s,UINT w,UINT h,IDirect3DPixelShader9* ps,UINT samplers) noexcept{
    HRESULT hr=S_OK;
    for(UINT i=0;i<samplers&&SUCCEEDED(hr);++i){hr=call<SetTextureFn>(SetTexture)(device_,i,nullptr);}
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
    const bool density=f.density;float ready_fine=0,ready_far=0;
    if(density){
        if(!density_ready(f.width,f.height)||!density_status_.available)return refuse(E_INVALIDARG);
        for(double v:f.camera_world)if(!std::isfinite(v))return refuse(E_INVALIDARG);
        // The ramps come from this frame's prepare_density; a camera whose rays leave the resident
        // nodes reads nothing of that level (fine: far-only interior; far: no fog this frame).
        ready_far=density_->covers(1,f.camera_world)?density_status_.ready_far:0.f;
        ready_fine=density_->covers(0,f.camera_world)?density_status_.ready_fine:0.f;
        if(!(ready_far>0)){r.operation=S_FALSE;return finish(S_FALSE);} // same zero-device-call path as off
        if(f.depth_share==density_atlas_[0]||f.depth_share==density_atlas_[1]||f.depth_share==density_staging_[0]||f.depth_share==density_staging_[1])return refuse(E_INVALIDARG);
    } else if(!resources_ready(f.width,f.height,f.profile,f.recipe_id,f.field_generation))return refuse(E_INVALIDARG);
    if(f.depth_share==atlas_||f.depth_share==lit_||f.depth_share==scratch_||f.target==lit_surface_||f.target==scratch_surface_)return refuse(E_INVALIDARG);
    D3DSURFACE_DESC ds{},ss{};HRESULT hr=f.depth_share->GetLevelDesc(0,&ds);if(FAILED(hr))return refuse(hr);
    hr=f.target->GetDesc(&ss);if(FAILED(hr))return refuse(hr);
    if(ds.Width!=width_||ds.Height!=height_||ds.Format!=D3DFMT_A32B32G32R32F||ss.Width!=width_||ss.Height!=height_||ss.Format!=D3DFMT_A16B16G16R16F||
       ss.MultiSampleType!=D3DMULTISAMPLE_NONE||!(ss.Usage&D3DUSAGE_RENDERTARGET))return refuse(E_INVALIDARG);
    hr=same_device(device_,f.depth_share);if(FAILED(hr))return refuse(hr);
    hr=same_device(device_,f.target);if(FAILED(hr))return refuse(hr);
    // All map references are borrowed for this serialized transaction. Validation
    // failure disables only that map; device loss still aborts before any write.
    IDirect3DTexture9* shadow_maps[fog_cascade_max]{};
    float constants[fog_look_first_register+fog_look_rows][4]{};
    const auto& p=f.params;
    constants[0][0]=p.m00;constants[0][1]=p.m11;constants[0][2]=p.m20;constants[0][3]=p.m21;
    constants[1][0]=static_cast<float>(width_);constants[1][1]=static_cast<float>(height_);constants[1][2]=static_cast<float>(half_width_);constants[1][3]=static_cast<float>(half_height_);
    for(unsigned i=0;i<3;++i){constants[2][i]=p.world.origin_mod[i];constants[3][i]=p.world.sun_world[i];}
    constants[2][3]=base_sigma_*p.density_scale;constants[3][3]=fog_volume_horizon;
    if(density){
        constants[2][3]=density_config_.sigma*p.density_scale*ready_far;constants[3][3]=float(fog::kTaperEnd);
        for(int level=0;level<fog::kLevelCount;++level){fog::camera_local(level,f.camera_world,constants[22+level]);constants[22+level][3]=float(1.0/fog::kLevelDelta[level]);}
        for(unsigned i=0;i<3;++i)constants[24][i]=density_config_.chroma[i];
        constants[24][3]=ready_fine;
    }
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)constants[4+i][j]=p.world.inverse_columns[3*i+j];
    fog_phase_constants(p.anisotropy,p.decode_exponent,p.sun_radiance,constants[7],constants[8]);
    // Look preset: rows c25..c33, the sigma factor and an already created program variant. Nothing else changes.
    const unsigned look=density&&density_status_.looks&&f.look<fog_look_count?f.look:0u;
    if(look)constants[2][3]*=fog_look_constants(look,density_config_.look,density_config_.chroma,constants[8],f.look_phase,constants+fog_look_first_register,f.look_resolved);
    const unsigned variant=look>=2?1u:0u,constant_rows=look?fog_look_first_register+fog_look_rows:25u;
    IDirect3DPixelShader9* const density_march=look?look_march_[variant]:density_march_;
    IDirect3DPixelShader9* const density_composite=look?look_composite_:density_composite_;
    IDirect3DPixelShader9* const density_repair=look?look_repair_[variant]:density_repair_;
    r.look=look;
    constants[9][1]=.95f;constants[9][2]=.85f;constants[9][3]=10.f;
    for(unsigned i=0;i<std::min(f.count,fog_cascade_max);++i) {
        const auto& k=f.cascades[i];
        if(!k.valid||!k.map||!fog_shadow_current(f.frame,k.frame)||!fog_shadow_rows(k.rows,k.bias)||
           k.map==f.depth_share||k.map==atlas_||k.map==lit_||k.map==scratch_||(density&&(k.map==density_atlas_[0]||k.map==density_atlas_[1])))continue;
        D3DSURFACE_DESC desc{};
        hr=k.map->GetLevelDesc(0,&desc);if(lost(hr))return refuse(hr);if(FAILED(hr))continue;
        if(desc.Format!=D3DFMT_R32F||desc.Width<64||desc.Width!=desc.Height||
           desc.MultiSampleType!=D3DMULTISAMPLE_NONE)continue;
        hr=same_device(device_,k.map);if(lost(hr))return refuse(hr);if(FAILED(hr))continue;
        float (*block)[4]=constants+10+4*i;
        for(unsigned j=0;j<12;++j)block[j/4][j%4]=k.rows[j];
        block[3][0]=float(desc.Width);block[3][1]=1.f/float(desc.Width);block[3][2]=k.bias;block[3][3]=1.f;
        shadow_maps[i]=k.map;++r.cascades_bound;
    }
    if(look&&r.cascades_bound){
        // The look programs read two cascades: the two coarsest admitted maps move to slots 0 and 1.
        unsigned kept[fog_cascade_max]{},n=0;
        for(unsigned i=0;i<fog_cascade_max;++i)if(shadow_maps[i])kept[n++]=i;
        const unsigned first=n>fog_look_cascades?n-fog_look_cascades:0u;
        for(unsigned out=0;out<fog_cascade_max;++out){
            const unsigned from=first+out<n?kept[first+out]:fog_cascade_max;
            if(from==out)continue; // from > out otherwise: sources are never overwritten before they are read
            if(from<fog_cascade_max){std::memcpy(constants[10+4*out],constants[10+4*from],sizeof(float)*16);shadow_maps[out]=shadow_maps[from];}
            else {std::memset(constants[10+4*out],0,sizeof(float)*16);shadow_maps[out]=nullptr;}
        }
        r.cascades_bound=n-first;
    }
    constants[9][0]=r.cascades_bound?1.f:0.f;
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
        changed=true;record(FogStage::Normalize,normalize(density));
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
    const UINT samplers=density?8u:7u;
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,bind_target(lit_surface_,half_width_,half_height_,density?density_march:march_,samplers));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetPsConstantsFn>(SetPixelShaderConstantF)(device_,0,&constants[0][0],density?constant_rows:22u));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,0,f.depth_share));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,1,density?density_atlas_[0]:atlas_));
    if(density&&may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,7,density_atlas_[1]));
    for(unsigned i=0;i<fog_cascade_max&&may_draw&&SUCCEEDED(r.operation);++i)
        record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,4+i,shadow_maps[i]));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,quad(half_width_,half_height_));
    if(density&&may_draw&&SUCCEEDED(r.operation)){
        // Composite never marches (no atlas, no maps); repair marches only the pixels no half sample serves.
        record(FogStage::Composite,bind_target(f.target,width_,height_,density_composite,samplers));
        IDirect3DTexture9* composite_inputs[]={f.depth_share,nullptr,scratch_,lit_};
        for(UINT i=0;i<4&&SUCCEEDED(r.operation);++i)if(composite_inputs[i])record(FogStage::Composite,call<SetTextureFn>(SetTexture)(device_,i,composite_inputs[i]));
        if(SUCCEEDED(r.operation)){r.scene_write_started=true;record(FogStage::Composite,quad(width_,height_));}
        if(SUCCEEDED(r.operation))record(FogStage::Repair,bind_target(f.target,width_,height_,density_repair,samplers));
        float repair_c0[4];density_repair_projection(p,repair_c0);
        if(SUCCEEDED(r.operation))record(FogStage::Repair,call<SetPsConstantsFn>(SetPixelShaderConstantF)(device_,0,repair_c0,1));
        IDirect3DTexture9* repair_inputs[]={f.depth_share,density_atlas_[0],scratch_,nullptr,shadow_maps[0],shadow_maps[1],shadow_maps[2],density_atlas_[1]};
        for(UINT i=0;i<8&&SUCCEEDED(r.operation);++i)if(repair_inputs[i])record(FogStage::Repair,call<SetTextureFn>(SetTexture)(device_,i,repair_inputs[i]));
        if(SUCCEEDED(r.operation))r.applied=record(FogStage::Repair,quad(width_,height_));
    }
    if(!density&&may_draw&&SUCCEEDED(r.operation))record(FogStage::Composite,bind_target(f.target,width_,height_,composite_));
    if(!density&&may_draw&&SUCCEEDED(r.operation)){
        IDirect3DTexture9* textures[]={f.depth_share,atlas_,scratch_,lit_,shadow_maps[0],shadow_maps[1],shadow_maps[2]};
        for(UINT i=0;i<7&&SUCCEEDED(r.operation);++i)record(FogStage::Composite,call<SetTextureFn>(SetTexture)(device_,i,textures[i]));
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
