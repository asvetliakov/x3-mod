#include "fog_pass.h"
#include "../fog/fog_density_cache.h"
#include "ps3_program_slots.h"
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
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp).
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, CreateVertexBuffer = 26, CreateIndexBuffer = 27, UpdateSurface = 30, UpdateTexture = 31, StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawIndexedPrimitive = 82, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92, SetVertexShaderConstantF = 94,
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
using SetVsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using CreateVbFn = HRESULT(WINAPI*)(D, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
using CreateIbFn = HRESULT(WINAPI*)(D, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
// Only our authored programs are embedded (tools/shaders/generate_rigid_motion_pixel.py).
constexpr DWORD march_words[] = {
#include "fog_march_program_inc.h"
};
constexpr DWORD composite_words[] = {
#include "fog_composite_program_inc.h"
};
// Stored-density programs: the single look (src/fog/fog_density_*_look_ps.hlsl, FOG_LOOK). Repair is
// 510 of 512 ps_3_0 slots on the Microsoft table: nothing may be added to it
// (fog_density_shader_slots.py). The unshaped programs of the retired L0 preset are no longer
// created by the renderer; they survive as the shader fixture's parity reference only.
constexpr DWORD density_march_words[] = {
#include "fog_density_march_look_program_inc.h"
};
constexpr DWORD density_composite_words[] = {
#include "fog_density_composite_look_program_inc.h"
};
constexpr DWORD density_repair_words[] = {
#include "fog_density_repair_look_program_inc.h"
};
// The sun-visibility slice grid (docs/architecture/fog-shadow-pass.md, FogDensityConfig::shadow_pass): the pass over
// the 4x4-tile RGBA8 atlas and the look's march/repair reading it (FOG_SHADOW_PASS). Created beside the look programs
// when the pass is requested; the *_look pair above stays the control variant.
constexpr DWORD density_visibility_words[] = {
#include "fog_density_visibility_grid_program_inc.h"
};
constexpr DWORD density_march_grid_words[] = {
#include "fog_density_march_grid_program_inc.h"
};
constexpr DWORD density_repair_grid_words[] = {
#include "fog_density_repair_grid_program_inc.h"
};
// Dust motes (docs/architecture/fog-dust-motes.md, FogDensityConfig::motes): the capsule vertex program and the pixel
// program in the in-march and grid variants, drawn after the repair; created at prepare_density only with the option.
constexpr DWORD mote_vertex_words[] = {
#include "fog_dust_motes_vertex_program_inc.h"
};
constexpr DWORD mote_look_words[] = {
#include "fog_dust_motes_look_program_inc.h"
};
constexpr DWORD mote_grid_words[] = {
#include "fog_dust_motes_grid_program_inc.h"
};
// Stream 0: unit seed xyz, corner xy (fog_mote_vertex_bytes).
constexpr D3DVERTEXELEMENT9 mote_declaration[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    D3DDECL_END()
};
constexpr double mote_drift_period=8.0; // seconds per drift cycle
constexpr double mote_cut_cosine=.8660254037844386; // a view axis turning more than 30 degrees in one frame is a cut
constexpr unsigned density_required_slots=512;
constexpr unsigned fog_constant_rows=fog_grid_first_register+fog_grid_rows; // c0..c41 of a stored-density frame
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
void FogPass::release_grid() noexcept { drop(grid_surface_);drop(grid_);grid_width_=grid_height_=0; }
void FogPass::release_motes() noexcept { drop(mote_vb_);drop(mote_ib_);mote_built_count_=0;mote_built_seed_=0;mote_previous_valid_=false; }
void FogPass::release_targets() noexcept {
    drop(lit_surface_);drop(scratch_surface_);drop(lit_);drop(scratch_);drop(block_);release_grid();release_motes();
    width_=height_=half_width_=half_height_=0;
}
void FogPass::release_density_default() noexcept {
    for(unsigned i=0;i<2;++i){drop(density_atlas_surface_[i]);drop(density_atlas_[i]);}
    release_grid();
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
    if(!fog_field::is_file_profile(cached_profile_))return false;
    // A later packet failure may disable the row; its constants stay those of the decoded field.
    const auto* table=fog_field::family_table();
    const auto* file_row=table?table->row(cached_profile_):nullptr;
    if(!file_row)return false;
    for(unsigned c=0;c<3;++c)chroma[c]=file_row->chroma[c];
    *sigma=base_sigma_;return true;
}
void FogPass::detach() noexcept {
    PreserveCpuState guard;release_targets();drop(atlas_);
    release_density_default();
    for(unsigned i=0;i<2;++i){drop(density_staging_surface_[i]);drop(density_staging_[i]);}
    drop(density_march_);drop(density_composite_);drop(density_repair_);
    drop(density_visibility_);drop(density_march_grid_);drop(density_repair_grid_);
    drop(mote_vs_);drop(mote_declaration_);drop(mote_ps_);drop(mote_ps_grid_);
    mote_caps_=motes_refused_=false;mote_max_index_=mote_max_primitives_=0;adapter_format_=D3DFMT_UNKNOWN;mote_report_={};
    fog::DensityCache::retire(density_);density_=nullptr; // joins the worker; a cache it had to abandon is leaked, not freed
    density_config_={};density_status_={};density_refused_=false;grid_refused_=false;ps30_slots_=0;drop(march_);drop(composite_);drop(quad_vs_);drop(quad_declaration_);
    device_=nullptr;vtable_=nullptr;caps_={};reset_pending_=false;disarm_field();cached_profile_=fog_field::Profile::None;cached_packet_=0;
    field_recipe_=0;base_sigma_=0;std::vector<std::uint16_t>().swap(atlas_bytes_);
    render_targets_=streams_=max_width_=max_height_=0;
}
// Reset keeps the worker, both CPU caches and the SYSTEMMEM staging textures; only the DEFAULT
// atlases go, and the cache re-uploads every committed tile under the normal budget afterwards.
void FogPass::before_reset() noexcept {PreserveCpuState guard;release_targets();drop(atlas_);release_density_default();disarm_field();reset_pending_=device_!=nullptr;} // release_targets drops the mote VB/IB
void FogPass::after_reset(HRESULT hr) noexcept {PreserveCpuState guard;if(SUCCEEDED(hr))reset_pending_=false;}
unsigned FogPass::references() const noexcept {
    unsigned n=0;
    for(unsigned i=0;i<2;++i)n+=(density_staging_[i]!=nullptr)+(density_atlas_[i]!=nullptr)+(density_staging_surface_[i]!=nullptr)+(density_atlas_surface_[i]!=nullptr);
    n+=(density_march_!=nullptr)+(density_composite_!=nullptr)+(density_repair_!=nullptr);
    n+=(density_visibility_!=nullptr)+(density_march_grid_!=nullptr)+(density_repair_grid_!=nullptr)+(grid_!=nullptr)+(grid_surface_!=nullptr);
    n+=(mote_vs_!=nullptr)+(mote_declaration_!=nullptr)+(mote_ps_!=nullptr)+(mote_ps_grid_!=nullptr)+(mote_vb_!=nullptr)+(mote_ib_!=nullptr);
    for(const void* p:{static_cast<void*>(atlas_),static_cast<void*>(lit_),static_cast<void*>(scratch_),static_cast<void*>(lit_surface_),static_cast<void*>(scratch_surface_),static_cast<void*>(march_),static_cast<void*>(composite_),static_cast<void*>(quad_vs_),static_cast<void*>(quad_declaration_),static_cast<void*>(block_)})n+=p!=nullptr;
    return n;
}
HRESULT FogPass::attach(D d,void* const* native,const D3DCAPS9& caps,D3DFORMAT format) noexcept {
    PreserveCpuState guard;detach();
    auto refuse=[&](const char* reason,HRESULT hr=D3DERR_NOTAVAILABLE){device_=nullptr;vtable_=nullptr;caps_.reason=reason;return hr;};
    if(!d||!native)return refuse("device_native_table",E_INVALIDARG);
    if(caps.PixelShaderVersion<D3DPS_VERSION(3,0)||caps.VertexShaderVersion<D3DVS_VERSION(3,0))return refuse("shader_model3");
    for(auto p:{std::pair{march_words,std::size(march_words)},std::pair{composite_words,std::size(composite_words)}}){
        const unsigned slots=ps3_program_slots(reinterpret_cast<const std::uint32_t*>(p.first),p.second);
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
        // rgba8_rt: the visibility grid's A8R8G8B8 target (bilinear filtering of that format is baseline for every D3D9 device).
        for(auto q:{Query{0,D3DFMT_A32B32G32R32F,"rgba32f_texture"},Query{D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,"fp16_rt"},Query{D3DUSAGE_QUERY_FILTER,D3DFMT_A16B16G16R16F,"fp16_filter_query"},Query{D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,"rgba8_rt"}}){
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
    // The dust motes' device limits, kept for a later request (plain copies, no query): additive blending with an
    // explicit ADD operation, the vertex program's constant rows, and the 16-bit index range of the largest N.
    mote_caps_=caps.MaxVertexShaderConst>=fog_mote_vs_rows&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_BLENDOP)&&(caps.SrcBlendCaps&D3DPBLENDCAPS_ONE)&&(caps.DestBlendCaps&D3DPBLENDCAPS_ONE);
    mote_max_index_=caps.MaxVertexIndex;mote_max_primitives_=caps.MaxPrimitiveCount;adapter_format_=format;
    caps_.programs=hr;caps_.enabled=true;caps_.reason="";return S_OK;
}
HRESULT FogPass::prepare_field(void* module,fog_field::Profile profile) noexcept {
    PreserveCpuState guard;
    if(profile==fog_field::Profile::None){disarm_field();return S_FALSE;}
    if(!device_||!caps_.enabled){disarm_field();return E_INVALIDARG;}
    if(reset_pending_){disarm_field();return D3DERR_DEVICENOTRESET;}
    if(profile==active_profile_&&atlas_)return S_OK;
    disarm_field();
    // Compiled profiles first (unchanged path); a file family's info comes from its table row.
    fog_field::ProfileInfo file_info{};
    const bool file_family=fog_field::is_file_profile(profile);
    const auto* info=file_family?(fog_field::family_info(profile,file_info)?&file_info:nullptr):fog_field::profile_info(profile);
    if(!info)return file_family?field_row_disabled:E_INVALIDARG;
    // File families sharing one packet (info->profile is the packet's own id) share the decoded
    // field and the atlas: only the row constants change.
    const bool same_packet=file_family&&cached_packet_!=0&&cached_packet_==static_cast<std::uint32_t>(info->profile);
    if(cached_profile_!=profile&&same_packet){cached_profile_=profile;base_sigma_=info->base_sigma;}
    if(cached_profile_!=profile){
        drop(atlas_);cached_profile_=fog_field::Profile::None;cached_packet_=0;field_recipe_=0;base_sigma_=0;
        const auto decoded=file_family?fog_field::decode_family(profile,atlas_bytes_):fog_field::decode_from_resource(module,profile,atlas_bytes_);
        if(!decoded)return file_family?field_row_disabled:static_cast<HRESULT>(decoded.hresult);
        cached_profile_=profile;field_recipe_=info->recipe_id;base_sigma_=info->base_sigma;
        cached_packet_=file_family?static_cast<std::uint32_t>(info->profile):0;
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
        // Per program, not only the device's ps_3_0 count: repair sits at 510 of the 512 slots a ps_3_0
        // device has to offer, so a program that grew past the ceiling must refuse before it is created.
        for(auto p:{std::pair{density_march_words,std::size(density_march_words)},std::pair{density_composite_words,std::size(density_composite_words)},std::pair{density_repair_words,std::size(density_repair_words)}}){
            const unsigned slots=ps3_program_slots(reinterpret_cast<const std::uint32_t*>(p.first),p.second);
            if(!slots||slots>=density_required_slots)return refuse("density_compiled_slots",D3DERR_NOTAVAILABLE);
        }
        HRESULT hr=call<CreatePsFn>(CreatePixelShader)(device_,density_march_words,&density_march_);
        if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,density_composite_words,&density_composite_);
        if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,density_repair_words,&density_repair_);
        // Pixel shaders survive Reset; nothing is created on a draw path. A refusal is final: with the
        // retired presets gone there is no unshaped fallback, so the stored path stays off (legacy untouched).
        if(FAILED(hr)){drop(density_march_);drop(density_composite_);drop(density_repair_);if(lost(hr)){reset_pending_=true;return hr;}return refuse("density_program_create",hr);}
    }
    // The visibility grid is optional: a failure to build it (not a lost device) falls back to the in-march programs
    // for the rest of the attachment and says why in density_status().shadow_pass_refused; the stored fog stays.
    auto refuse_grid=[&](const char* reason){grid_refused_=true;density_config_.shadow_pass=false;density_status_.shadow_pass_refused=reason;
        drop(density_visibility_);drop(density_march_grid_);drop(density_repair_grid_);release_grid();};
    if(density_config_.shadow_pass&&!density_visibility_){
        // The visibility grid's three programs, the same per-program ceiling; created once, never on a draw path.
        bool fits=true;
        for(auto p:{std::pair{density_visibility_words,std::size(density_visibility_words)},std::pair{density_march_grid_words,std::size(density_march_grid_words)},std::pair{density_repair_grid_words,std::size(density_repair_grid_words)}}){
            const unsigned slots=ps3_program_slots(reinterpret_cast<const std::uint32_t*>(p.first),p.second);
            fits=fits&&slots&&slots<density_required_slots;
        }
        if(!fits)refuse_grid("density_grid_compiled_slots");
        else{
            HRESULT hr=call<CreatePsFn>(CreatePixelShader)(device_,density_visibility_words,&density_visibility_);
            if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,density_march_grid_words,&density_march_grid_);
            if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,density_repair_grid_words,&density_repair_grid_);
            if(lost(hr)){drop(density_visibility_);drop(density_march_grid_);drop(density_repair_grid_);reset_pending_=true;return hr;}
            if(FAILED(hr))refuse_grid("density_grid_program_create");
        }
    }
    if(density_config_.shadow_pass&&!grid_&&width_&&height_){
        // The 4x4-tile RGBA8 atlas of the current targets: a quarter-resolution texel per 4x4 full pixels, four slices per
        // texel. Sized with the targets (release_targets drops it: resize, before_reset, detach) and re-created here.
        const UINT gw=fog_grid_extent(width_),gh=fog_grid_extent(height_),aw=gw*fog_grid_tiles,ah=gh*fog_grid_tiles;
        if(aw>max_width_||ah>max_height_)refuse_grid("density_grid_extent");
        else{
            HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,aw,ah,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&grid_,nullptr);
            if(SUCCEEDED(hr))hr=grid_->GetSurfaceLevel(0,&grid_surface_);
            if(lost(hr)){release_grid();reset_pending_=true;return hr;}
            if(FAILED(hr))refuse_grid("density_grid_target");
            else{grid_width_=aw;grid_height_=ah;++allocations_;}
        }
    }
    // The dust motes are optional like the grid: a capability, program or buffer failure (not a lost device) drops the
    // mote stage for the rest of the attachment and says why in density_status().motes_refused; the fog stays.
    auto refuse_motes=[&](const char* reason){motes_refused_=true;density_config_.dust_motes=false;density_status_.motes_refused=reason;
        drop(mote_vs_);drop(mote_declaration_);drop(mote_ps_);drop(mote_ps_grid_);release_motes();};
    if(density_config_.dust_motes&&!mote_vs_){
        if(const char* reason=mote_capabilities())refuse_motes(reason);
        else{
            bool fits=true;
            for(auto p:{std::pair{mote_look_words,std::size(mote_look_words)},std::pair{mote_grid_words,std::size(mote_grid_words)}}){
                const unsigned slots=ps3_program_slots(reinterpret_cast<const std::uint32_t*>(p.first),p.second);
                fits=fits&&slots&&slots<density_required_slots;
            }
            if(!fits)refuse_motes("mote_compiled_slots");
            else{
                HRESULT hr=call<CreateVsFn>(CreateVertexShader)(device_,mote_vertex_words,&mote_vs_);
                if(SUCCEEDED(hr))hr=call<CreateDeclarationFn>(CreateVertexDeclaration)(device_,mote_declaration,&mote_declaration_);
                if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,mote_look_words,&mote_ps_);
                if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(device_,mote_grid_words,&mote_ps_grid_);
                if(lost(hr)){drop(mote_vs_);drop(mote_declaration_);drop(mote_ps_);drop(mote_ps_grid_);reset_pending_=true;return hr;}
                if(FAILED(hr))refuse_motes("mote_program_create");
            }
        }
    }
    if(density_config_.dust_motes&&mote_vs_&&(!mote_vb_||mote_built_count_!=density_config_.motes.count||mote_built_seed_!=density_config_.motes.seed)){
        const HRESULT hr=mote_buffers();
        if(lost(hr)){reset_pending_=true;return hr;}
        if(FAILED(hr))refuse_motes("mote_buffers");
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
// The dust motes' documented prerequisites: the attach-time limits, then the FP16 target's post-pixel-shader blending.
const char* FogPass::mote_capabilities() noexcept {
    const unsigned n=density_config_.motes.count;
    if(n<fog_mote_count_min||n>fog_mote_count_max)return "mote_count";
    if(!mote_caps_)return "mote_blend_caps";
    if(mote_max_index_<4u*n-1u||mote_max_primitives_<2u*n)return "mote_index_limits";
    IDirect3D9* api=nullptr;D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr=call<GetD3DFn>(GetDirect3D)(device_,&api);
    if(SUCCEEDED(hr)&&!api)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=call<GetCreationFn>(GetCreationParameters)(device_,&creation);
    if(SUCCEEDED(hr))hr=api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,adapter_format_,D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_TEXTURE,D3DFMT_A16B16G16R16F);
    drop(api);
    return hr==D3D_OK?nullptr:"mote_fp16_blending";
}
// N capsules: four corners of one unit seed each (fog_mote_seed), two triangles per capsule. Static DEFAULT buffers written
// once through Lock; released with the targets (Reset, resize, detach) and re-created here by the next prepare_density.
HRESULT FogPass::mote_buffers() noexcept {
    release_motes();
    const unsigned n=density_config_.motes.count;const std::uint32_t seed=density_config_.motes.seed;
    HRESULT hr=call<CreateVbFn>(CreateVertexBuffer)(device_,n*4u*fog_mote_vertex_bytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&mote_vb_,nullptr);
    if(SUCCEEDED(hr))hr=call<CreateIbFn>(CreateIndexBuffer)(device_,n*6u*unsigned(sizeof(std::uint16_t)),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&mote_ib_,nullptr);
    void* bits=nullptr;
    if(SUCCEEDED(hr))hr=mote_vb_->Lock(0,0,&bits,0);
    if(SUCCEEDED(hr)){
        if(bits){
            static constexpr float corners[4][2]={{-1.f,-1.f},{1.f,-1.f},{-1.f,1.f},{1.f,1.f}};
            float* out=static_cast<float*>(bits);
            for(unsigned i=0;i<n;++i){
                float s[3];fog_mote_seed(i,seed,s);
                for(const auto& c:corners){*out++=s[0];*out++=s[1];*out++=s[2];*out++=c[0];*out++=c[1];}
            }
        }
        const HRESULT unlock=mote_vb_->Unlock();
        hr=!bits?E_FAIL:unlock;
    }
    bits=nullptr;
    if(SUCCEEDED(hr))hr=mote_ib_->Lock(0,0,&bits,0);
    if(SUCCEEDED(hr)){
        if(bits){
            std::uint16_t* out=static_cast<std::uint16_t*>(bits);
            for(unsigned i=0;i<n;++i){
                const std::uint16_t b=static_cast<std::uint16_t>(4u*i);
                for(unsigned k:{0u,1u,2u,2u,1u,3u})*out++=static_cast<std::uint16_t>(b+k);
            }
        }
        const HRESULT unlock=mote_ib_->Unlock();
        hr=!bits?E_FAIL:unlock;
    }
    if(FAILED(hr)){release_motes();return hr;}
    mote_built_count_=n;mote_built_seed_=seed;++allocations_;return S_OK;
}
// The vertex program's twelve rows (fog_dust_motes_vs.hlsl). CPU only: the world->view rotation (the inverse of the fog's
// view->world rows, double), the camera modulo the cube side, the previous drawn frame's basis when it is the frame
// before this one, the drift phases and the brightness (GAIN x density scale x far ramp). False when a value is unusable.
bool FogPass::mote_constants(const FogFrame& f,float ready_far,float k[fog_mote_vs_rows][4],FogMoteReport& report,double r[9]) const noexcept {
    const FogMoteTuning& t=density_config_.motes;const FogParams& p=f.params;
    double m[3][3];
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)m[row][col]=p.world.inverse_columns[3*col+row]; // world = view m
    const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if(!std::isfinite(det)||!(std::abs(det)>.5))return false;
    r[0]=(m[1][1]*m[2][2]-m[1][2]*m[2][1])/det;r[1]=(m[0][2]*m[2][1]-m[0][1]*m[2][2])/det;r[2]=(m[0][1]*m[1][2]-m[0][2]*m[1][1])/det;
    r[3]=(m[1][2]*m[2][0]-m[1][0]*m[2][2])/det;r[4]=(m[0][0]*m[2][2]-m[0][2]*m[2][0])/det;r[5]=(m[0][2]*m[1][0]-m[0][0]*m[1][2])/det;
    r[6]=(m[1][0]*m[2][1]-m[1][1]*m[2][0])/det;r[7]=(m[0][1]*m[2][0]-m[0][0]*m[2][1])/det;r[8]=(m[0][0]*m[1][1]-m[0][1]*m[1][0])/det;
    const double radius=t.radius,side=2*radius,seconds=std::isfinite(f.mote_seconds)?f.mote_seconds:0.;
    double delta[3]{},shift=0,turn=1;
    if(mote_previous_valid_){
        double forward=0,a=0,b=0;
        for(unsigned i=0;i<3;++i){
            delta[i]=f.camera_world[i]-mote_previous_camera_[i];shift+=delta[i]*delta[i];
            forward+=r[3*i+2]*mote_previous_rotation_[3*i+2];a+=r[3*i+2]*r[3*i+2];b+=mote_previous_rotation_[3*i+2]*mote_previous_rotation_[3*i+2];
        }
        shift=std::sqrt(shift);turn=a>0&&b>0?forward/std::sqrt(a*b):-1.;
    }
    const bool streak=mote_previous_valid_&&!f.mote_cut&&f.frame==mote_previous_frame_+1&&shift<=radius&&turn>=mote_cut_cosine;
    const double* before=streak?mote_previous_rotation_:r;
    for(unsigned j=0;j<3;++j)for(unsigned i=0;i<3;++i){k[1+j][i]=float(r[3*i+j]);k[4+j][i]=float(before[3*i+j]);}
    k[0][0]=p.m00;k[0][1]=p.m11;k[0][2]=p.m20-quad_pixel_centre_m20(width_);k[0][3]=p.m21-quad_pixel_centre_m21(height_);
    k[1][3]=float(side);k[2][3]=float(1/side);k[3][3]=float(radius);
    k[4][3]=t.near_fade;k[5][3]=float(1/(.25*radius));k[6][3]=float(1./t.near_fade);
    for(unsigned i=0;i<3;++i){
        double wrapped=std::fmod(f.camera_world[i],side);if(wrapped<0)wrapped+=side;
        k[7][i]=float(wrapped);if(k[7][i]>=float(side))k[7][i]=0.f;
        k[8][i]=streak?float(delta[i]):0.f;
    }
    k[7][3]=streak?1.f:0.f;k[8][3]=t.streak;
    const double two_pi=6.283185307179586,now=seconds/mote_drift_period,then=streak?mote_previous_seconds_/mote_drift_period:now;
    k[9][0]=float(two_pi*(now-std::floor(now)));k[9][1]=float(two_pi*(then-std::floor(then)));k[9][2]=t.drift;k[9][3]=t.gain*p.density_scale*ready_far;
    k[10][0]=t.size;k[10][1]=t.max_px;k[10][2]=float(double(t.size)*radius);k[10][3]=t.soft;
    k[11][0]=.5f*float(width_);k[11][1]=.5f*float(height_);k[11][2]=2.f/float(width_);k[11][3]=2.f/float(height_);
    for(unsigned i=0;i<fog_mote_vs_rows;++i)for(unsigned j=0;j<4;++j)if(!std::isfinite(k[i][j]))return false;
    report.streak=streak;report.count=t.count;
    report.shift_px=mote_previous_valid_?float(shift/radius*.5*double(height_)*double(p.m11)):0.f;
    return true;
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
    density_config_=config; // density_resources creates the variant the config asks for
    if(grid_refused_)density_config_.shadow_pass=false; // a refused grid stays refused until detach: the in-march programs draw
    if(motes_refused_||density_config_.motes.count==0)density_config_.dust_motes=false; // refused motes stay refused; no option, no stage
    HRESULT hr=density_resources();if(FAILED(hr))return hr;
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
    if(f.depth_share==atlas_||f.depth_share==lit_||f.depth_share==scratch_||f.depth_share==grid_||f.target==lit_surface_||f.target==scratch_surface_||f.target==grid_surface_)return refuse(E_INVALIDARG);
    const bool grid=density&&density_config_.shadow_pass;
    D3DSURFACE_DESC ds{},ss{};HRESULT hr=f.depth_share->GetLevelDesc(0,&ds);if(FAILED(hr))return refuse(hr);
    hr=f.target->GetDesc(&ss);if(FAILED(hr))return refuse(hr);
    if(ds.Width!=width_||ds.Height!=height_||ds.Format!=D3DFMT_A32B32G32R32F||ss.Width!=width_||ss.Height!=height_||ss.Format!=D3DFMT_A16B16G16R16F||
       ss.MultiSampleType!=D3DMULTISAMPLE_NONE||!(ss.Usage&D3DUSAGE_RENDERTARGET))return refuse(E_INVALIDARG);
    hr=same_device(device_,f.depth_share);if(FAILED(hr))return refuse(hr);
    hr=same_device(device_,f.target);if(FAILED(hr))return refuse(hr);
    // All map references are borrowed for this serialized transaction. Validation
    // failure disables only that map; device loss still aborts before any write.
    IDirect3DTexture9* shadow_maps[fog_cascade_max]{};FogGridCascade grid_cascades[fog_cascade_max]{};
    float constants[fog_constant_rows][4]{};
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
    // The look: rows c25..c35 and the sigma factor, bound by the stored path only. Nothing else changes.
    if(density)constants[2][3]*=fog_look_constants(density_config_.look,density_config_.chroma,constants[8],f.look_phase,constants+fog_look_first_register,f.look_resolved);
    constants[9][1]=.95f;constants[9][2]=.85f;constants[9][3]=10.f;
    for(unsigned i=0;i<std::min(f.count,fog_cascade_max);++i) {
        const auto& k=f.cascades[i];
        if(!k.valid||!k.map||!fog_shadow_current(f.frame,k.frame)||!fog_shadow_rows(k.rows,k.bias)||
           k.map==f.depth_share||k.map==atlas_||k.map==lit_||k.map==scratch_||k.map==grid_||(density&&(k.map==density_atlas_[0]||k.map==density_atlas_[1])))continue;
        D3DSURFACE_DESC desc{};
        hr=k.map->GetLevelDesc(0,&desc);if(lost(hr))return refuse(hr);if(FAILED(hr))continue;
        if(desc.Format!=D3DFMT_R32F||desc.Width<64||desc.Width!=desc.Height||
           desc.MultiSampleType!=D3DMULTISAMPLE_NONE)continue;
        hr=same_device(device_,k.map);if(lost(hr))return refuse(hr);if(FAILED(hr))continue;
        float (*block)[4]=constants+10+4*i;
        for(unsigned j=0;j<12;++j)block[j/4][j%4]=k.rows[j];
        block[3][0]=float(desc.Width);block[3][1]=1.f/float(desc.Width);block[3][2]=k.bias;block[3][3]=1.f;
        shadow_maps[i]=k.map;++r.cascades_bound;
        grid_cascades[i].texel_world=k.texel_world;grid_cascades[i].range_world=k.depth_range; // fog_grid_constants validates them
    }
    unsigned grid_replaced_repair=0; // the grid report: map binds the in-march repair would issue (min(admitted, 2))
    if(grid){
        grid_report_=FogGridReport{};grid_report_.frame=f.frame;
        for(unsigned i=0;i<fog_cascade_max;++i)if(shadow_maps[i])grid_report_.cascades|=1u<<i;
        grid_replaced_repair=std::min(r.cascades_bound,fog_look_cascades);
        // The pass reads every admitted cascade, finest first, with the cross-fade: no remap to the two coarsest. A column
        // cap the slice law cannot divide (not finite or below 12040) drops every cascade for the frame: lit fog, no grid read.
        if(!fog_grid_constants(density_config_.look,constants[fog_look_first_register][3],width_,height_,grid_cascades,f.look_phase,f.look_resolved,constants+fog_grid_first_register)){
            for(unsigned i=0;i<fog_cascade_max;++i){shadow_maps[i]=nullptr;std::memset(constants[10+4*i],0,sizeof(float)*16);}
            r.cascades_bound=0;density_status_.shadow_pass_refused="grid_column_cap";
            grid_report_.cascades=0;grid_report_.unshadowed="grid_column_cap";
        } else {
            for(unsigned i=0;i<fog_cascade_max;++i)grid_report_.kernel[i]=constants[fog_grid_first_register+i][2];
            grid_report_.far_width=constants[fog_grid_first_register+3][1];grid_report_.frame_term=constants[fog_grid_first_register+5][3];
            if(!r.cascades_bound)grid_report_.unshadowed="no_cascade";
        }
    } else if(density&&r.cascades_bound){
        // The look program reads two cascades: the two coarsest admitted maps move to slots 0 and 1.
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
    // Dust motes (fog-dust-motes.md): the last stage, after the repair, only while the option's toggle is latched on and
    // its programs and buffers exist. Rows and report are CPU work of this frame; nothing here touches the device.
    const bool motes_wanted=density&&density_config_.dust_motes&&!motes_refused_;
    float mote_rows[fog_mote_vs_rows][4]{};double mote_rotation[9]{};bool mote_stage=false;
    if(motes_wanted){
        mote_report_=FogMoteReport{};mote_report_.frame=f.frame;mote_report_.count=density_config_.motes.count;
        mote_report_.shadow=!r.cascades_bound?"none":grid?"grid":"in_march";
        mote_stage=mote_vs_&&mote_declaration_&&mote_ps_&&mote_ps_grid_&&mote_vb_&&mote_ib_&&mote_built_count_==density_config_.motes.count&&
            mote_constants(f,ready_far,mote_rows,mote_report_,mote_rotation);
    }
    if(!mote_stage)mote_previous_valid_=false; // no streak across a frame without motes
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
    // The visibility grid: one quad over the atlas with the maps at s4..s6 before the march, inside the same bracket
    // (a failure fails the transaction like a failed march). No cascade: skipped, and shadow_select.x = 0 keeps the
    // march from reading the grid at all (today's lit path). Constants are device state: uploaded once here.
    const bool draw_grid=grid&&r.cascades_bound>0;bool constants_set=false;
    unsigned grid_calls_start=0;if(grid)grid_calls_start=calls_;
    if(draw_grid&&may_draw&&SUCCEEDED(r.operation)){
        const HRESULT bind=bind_target(grid_surface_,grid_width_,grid_height_,density_visibility_,samplers);
        grid_report_.bind=bind;record(FogStage::Visibility,bind);
    }
    if(draw_grid&&may_draw&&SUCCEEDED(r.operation)){constants_set=record(FogStage::Visibility,call<SetPsConstantsFn>(SetPixelShaderConstantF)(device_,0,&constants[0][0],fog_constant_rows));}
    for(unsigned i=0;i<fog_cascade_max&&draw_grid&&may_draw&&SUCCEEDED(r.operation);++i)
        record(FogStage::Visibility,call<SetTextureFn>(SetTexture)(device_,4+i,shadow_maps[i]));
    if(draw_grid&&may_draw&&SUCCEEDED(r.operation))r.grid=record(FogStage::Visibility,quad(grid_width_,grid_height_));
    if(grid){
        // The in-march march uploads its own constants; the grid frame's single upload above replaces it.
        grid_report_.drawn=r.grid;grid_report_.calls=calls_-grid_calls_start;grid_report_.net_calls=int(grid_report_.calls)-int(constants_set);
        if(draw_grid&&!r.grid)grid_report_.unshadowed="failed";
    }
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,bind_target(lit_surface_,half_width_,half_height_,density?(grid?density_march_grid_:density_march_):march_,samplers));
    if(may_draw&&SUCCEEDED(r.operation)&&!constants_set)record(FogStage::March,call<SetPsConstantsFn>(SetPixelShaderConstantF)(device_,0,&constants[0][0],density?fog_constant_rows:22u));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,0,f.depth_share));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,1,density?density_atlas_[0]:atlas_));
    if(density&&may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,7,density_atlas_[1]));
    if(grid){
        // The grid at s4, LINEAR (normalize left s4 POINT for the pass's map); the block bracket restores the sampler.
        // The in-march march binds s4..s6 here (three calls) whenever it gets this far.
        const unsigned march_start=calls_;const bool reached=may_draw&&SUCCEEDED(r.operation);
        if(draw_grid&&may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetSamplerFn>(SetSamplerState)(device_,4,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));
        if(draw_grid&&may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetSamplerFn>(SetSamplerState)(device_,4,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
        if(draw_grid&&may_draw&&SUCCEEDED(r.operation))record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,4,grid_));
        grid_report_.calls+=calls_-march_start;grid_report_.net_calls+=int(calls_-march_start)-(reached?3:0);
    } else for(unsigned i=0;i<fog_cascade_max&&may_draw&&SUCCEEDED(r.operation);++i)
        record(FogStage::March,call<SetTextureFn>(SetTexture)(device_,4+i,shadow_maps[i]));
    if(may_draw&&SUCCEEDED(r.operation))record(FogStage::March,quad(half_width_,half_height_));
    if(density&&may_draw&&SUCCEEDED(r.operation)){
        // Composite never marches (no atlas, no maps); repair marches only the pixels no half sample serves.
        record(FogStage::Composite,bind_target(f.target,width_,height_,density_composite_,samplers));
        IDirect3DTexture9* composite_inputs[]={f.depth_share,nullptr,scratch_,lit_};
        for(UINT i=0;i<4&&SUCCEEDED(r.operation);++i)if(composite_inputs[i])record(FogStage::Composite,call<SetTextureFn>(SetTexture)(device_,i,composite_inputs[i]));
        if(SUCCEEDED(r.operation)){r.scene_write_started=true;record(FogStage::Composite,quad(width_,height_));}
        if(SUCCEEDED(r.operation))record(FogStage::Repair,bind_target(f.target,width_,height_,grid?density_repair_grid_:density_repair_,samplers));
        float repair_c0[4];density_repair_projection(p,repair_c0);
        if(SUCCEEDED(r.operation))record(FogStage::Repair,call<SetPsConstantsFn>(SetPixelShaderConstantF)(device_,0,repair_c0,1));
        IDirect3DTexture9* repair_inputs[]={f.depth_share,density_atlas_[0],scratch_,nullptr,grid?(draw_grid?grid_:nullptr):shadow_maps[0],grid?nullptr:shadow_maps[1],grid?nullptr:shadow_maps[2],density_atlas_[1]};
        UINT slot=0; // the slots bound (or tried) before a failure stopped the loop
        for(;slot<8&&SUCCEEDED(r.operation);++slot)if(repair_inputs[slot])record(FogStage::Repair,call<SetTextureFn>(SetTexture)(device_,slot,repair_inputs[slot]));
        if(grid&&slot>4){const unsigned bound=draw_grid?1u:0u;grid_report_.calls+=bound;grid_report_.net_calls+=int(bound)-int(grid_replaced_repair);}
        if(SUCCEEDED(r.operation))r.applied=record(FogStage::Repair,quad(width_,height_));
        if(mote_stage&&r.applied&&SUCCEEDED(r.operation)){
            // ONE/ONE on the still bound FP16 target (no SetRenderTarget) with the constants and samplers the repair left:
            // the capsules read RT2 at s0, the atlases at s1/s7 and the maps (s4-s5) or the grid (s4). The block Apply puts
            // back the programs, declaration, indices, vertex constants and render states; the stream tuples are restored
            // explicitly. CLIPPING and BLENDOP are set: normalize leaves clipping off and never touches the blend operation.
            const unsigned start=calls_;HRESULT hr_m=call<SetVsFn>(SetVertexShader)(device_,mote_vs_);
            if(SUCCEEDED(hr_m))hr_m=call<SetDeclarationFn>(SetVertexDeclaration)(device_,mote_declaration_);
            if(SUCCEEDED(hr_m))hr_m=call<SetStreamFn>(SetStreamSource)(device_,0,mote_vb_,0,fog_mote_vertex_bytes);
            if(SUCCEEDED(hr_m))hr_m=call<SetIndicesFn>(SetIndices)(device_,mote_ib_);
            if(SUCCEEDED(hr_m))hr_m=call<SetPsFn>(SetPixelShader)(device_,grid?mote_ps_grid_:mote_ps_);
            if(SUCCEEDED(hr_m))hr_m=call<SetVsConstantsFn>(SetVertexShaderConstantF)(device_,0,&mote_rows[0][0],fog_mote_vs_rows);
            for(auto state:{std::pair{D3DRS_CLIPPING,DWORD(TRUE)},std::pair{D3DRS_ALPHABLENDENABLE,DWORD(TRUE)},std::pair{D3DRS_SRCBLEND,DWORD(D3DBLEND_ONE)},
                            std::pair{D3DRS_DESTBLEND,DWORD(D3DBLEND_ONE)},std::pair{D3DRS_BLENDOP,DWORD(D3DBLENDOP_ADD)}})
                if(SUCCEEDED(hr_m))hr_m=call<SetRsFn>(SetRenderState)(device_,state.first,state.second);
            const UINT n=density_config_.motes.count;
            if(SUCCEEDED(hr_m))hr_m=call<DrawIndexedFn>(DrawIndexedPrimitive)(device_,D3DPT_TRIANGLELIST,0,0,4*n,0,2*n);
            mote_report_.calls=calls_-start;
            if(SUCCEEDED(hr_m)){
                r.motes=mote_report_.drawn=true;mote_previous_valid_=true;mote_previous_frame_=f.frame;mote_previous_seconds_=f.mote_seconds;
                for(unsigned i=0;i<3;++i)mote_previous_camera_[i]=f.camera_world[i];
                for(unsigned i=0;i<9;++i)mote_previous_rotation_[i]=mote_rotation[i];
            } else {
                mote_previous_valid_=false;
                if(lost(hr_m))record(FogStage::Motes,hr_m); // the device went: the transaction's loss path owns it
                else{motes_refused_=true;density_config_.dust_motes=false;density_status_.motes_refused="mote_draw";} // the fog frame stands
            }
        } else if(mote_stage)mote_previous_valid_=false;
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
