#include "temporal_pass.h"
#include "quad_vertex_program.h"
#include "temporal_resolve_program.h"
#include "../temporal/sharpen.h"
#include <algorithm>
#include <cstring>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if(value) { value->Release(); value=nullptr; } }
bool lost(HRESULT hr) noexcept { return hr==D3DERR_DEVICELOST || hr==D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots, verified against the MinGW d3d9.h method order
// by verification/probe/abi_check.cpp. The pass calls the device only through
// these so a hooked device (the proxy's private vtable) can hand it the
// original table and never observe its own injected calls.
enum Slot : unsigned {
    GetDirect3D = 6, GetDeviceCaps = 7, GetDisplayMode = 8, GetCreationParameters = 9, CreateTexture = 23, StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, SetFVF = 89,
    CreateVertexShader = 91, SetVertexShader = 92, SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106,
    SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using CapsFn = HRESULT(WINAPI*)(D, D3DCAPS9*);
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetDisplayModeFn = HRESULT(WINAPI*)(D, UINT, D3DDISPLAYMODE*);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using StretchFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
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
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
template<class Resource> HRESULT same_device(IDirect3DDevice9* device,Resource* resource) noexcept {
    IDirect3DDevice9* owner=nullptr;HRESULT hr=resource->GetDevice(&owner);
    if(FAILED(hr))return hr;
    const bool same=owner==device;drop(owner);
    return same?S_OK:E_INVALIDARG;
}
HRESULT texture_input(IDirect3DDevice9* device,IDirect3DTexture9* texture,UINT w,UINT h,D3DFORMAT format, D3DFORMAT* depth_format=nullptr) noexcept {
    if(!texture)return E_INVALIDARG;
    D3DSURFACE_DESC desc{};
    HRESULT hr=texture->GetLevelDesc(0,&desc);if(FAILED(hr))return hr;
    if(desc.Width!=w||desc.Height!=h||(desc.Format!=format&&!(depth_format&&(desc.Format==D3DFMT_G32R32F||desc.Format==D3DFMT_A32B32G32R32F)))||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return E_INVALIDARG;
    if(depth_format)*depth_format=desc.Format;
    return same_device(device,texture);
}
// The 8-bit main target: a default-pool, non-multisampled 32-bit surface that
// StretchRect can read. It need not be a texture level. `format` receives its
// format (the draw mode's staging texture matches it).
HRESULT surface_input(IDirect3DDevice9* device,IDirect3DSurface9* surface,UINT w,UINT h,D3DFORMAT* format) noexcept {
    if(!surface)return E_INVALIDARG;
    D3DSURFACE_DESC desc{};
    HRESULT hr=surface->GetDesc(&desc);if(FAILED(hr))return hr;
    if(desc.Width!=w||desc.Height!=h||(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8)||
       desc.Pool!=D3DPOOL_DEFAULT||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return E_INVALIDARG;
    *format=desc.Format;
    return same_device(device,surface);
}
}
// Everything a run touches beyond the state block: the render-target and depth
// bindings, viewport and scissor (SetRenderTarget resets the latter two). The
// block itself is owned by the pass and only captured/applied here.
struct TemporalPass::SavedState {
    const TemporalPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth=nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(const TemporalPass& p,IDirect3DStateBlock9* b,UINT n):pass(p),block(b),count(n){}
    ~SavedState(){for(auto& t:targets)drop(t);drop(depth);}
    HRESULT capture() noexcept {
        HRESULT hr=block->Capture();
        if(FAILED(hr))return hr;
        for(UINT i=0;i<count;++i){hr=pass.call<GetRtFn>(GetRenderTarget)(pass.device_,i,&targets[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return hr;}
        hr=pass.call<GetDepthFn>(GetDepthStencilSurface)(pass.device_,&depth);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return hr;
        hr=pass.call<GetViewportFn>(GetViewport)(pass.device_,&viewport);if(FAILED(hr))return hr;
        return pass.call<GetScissorFn>(GetScissorRect)(pass.device_,&scissor);
    }
    HRESULT restore() noexcept {
        HRESULT first=S_OK;
        auto attempt=[&](HRESULT hr){if(lost(hr)||(FAILED(hr)&&SUCCEEDED(first)))first=hr;return !lost(hr);};
        D d=pass.device_;
        // Unbind pass inputs before returning application RTs. Restore RT0 before
        // secondary RTs/DS, and viewport last because SetRenderTarget resets it.
        for(UINT i=0;i<16;++i)if(!attempt(pass.call<SetTextureFn>(SetTexture)(d,i,nullptr)))return first;
        for(UINT i=0;i<4;++i)if(!attempt(pass.call<SetTextureFn>(SetTexture)(d,D3DVERTEXTEXTURESAMPLER0+i,nullptr)))return first;
        if(!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d,nullptr)))return first;
        for(UINT i=1;i<count;++i)if(!attempt(pass.call<SetRtFn>(SetRenderTarget)(d,i,nullptr)))return first;
        for(UINT i=0;i<count;++i)if(!attempt(pass.call<SetRtFn>(SetRenderTarget)(d,i,targets[i])))return first;
        if(!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d,depth)))return first;
        if(!attempt(block->Apply()))return first;
        if(!attempt(pass.call<SetViewportFn>(SetViewport)(d,&viewport)))return first;
        attempt(pass.call<SetScissorFn>(SetScissorRect)(d,&scissor));
        return first;
    }
};
// The full-target strip through the vs_3_0 pass-through bound by normalize
// (clip space with the -0.5 pixel shift; quad_vertex_program.h), or the
// pre-transformed twin in fixture builds that selected it.
HRESULT TemporalPass::quad(UINT w,UINT h) noexcept {
    QuadVertex vertices[4];
    if(quad_fvf_)quad_vertices_xyzrhw(w,h,vertices);else quad_vertices(w,h,vertices);
    return call<DrawUpFn>(DrawPrimitiveUP)(device_,D3DPT_TRIANGLESTRIP,2,vertices,sizeof(QuadVertex));
}
HRESULT TemporalPass::normalize(UINT w,UINT h) noexcept {
    D d=device_;
#define STEP(call) do { const HRESULT hresult=(call); if(FAILED(hresult))return hresult; } while(false)
    for(UINT i=0;i<16;++i)STEP(call<SetTextureFn>(SetTexture)(d,i,nullptr));
    for(UINT i=0;i<4;++i)STEP(call<SetTextureFn>(SetTexture)(d,D3DVERTEXTEXTURESAMPLER0+i,nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d,nullptr));
    for(UINT i=1;i<render_targets_;++i)STEP(call<SetRtFn>(SetRenderTarget)(d,i,nullptr));
    // Every quad draws through the embedded vs_3_0 pass-through and its
    // declaration (D3D9 pairs ps_3_0 with vs_3_0); the fixture twin keeps the
    // pre-transformed fixed-function path to prove the two byte-identical.
    if(quad_fvf_){STEP(call<SetVsFn>(SetVertexShader)(d,nullptr));STEP(call<SetFvfFn>(SetFVF)(d,quad_fvf));}
    else{STEP(call<SetDeclarationFn>(SetVertexDeclaration)(d,quad_declaration_));STEP(call<SetVsFn>(SetVertexShader)(d,quad_vs_));}
    STEP(call<SetIndicesFn>(SetIndices)(d,nullptr));
    for(UINT i=0;i<streams_;++i)STEP(call<SetFreqFn>(SetStreamSourceFreq)(d,i,1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,
                   D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,
                   D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,
                   D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,
                   D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})STEP(call<SetRsFn>(SetRenderState)(d,state,FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d,D3DRS_VERTEXBLEND,D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d,D3DRS_WRAP0,0));
    STEP(call<SetRsFn>(SetRenderState)(d,D3DRS_FILLMODE,D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d,D3DRS_CULLMODE,D3DCULL_NONE));
    STEP(call<SetRsFn>(SetRenderState)(d,D3DRS_COLORWRITEENABLE,15));
    STEP(call<SetRsFn>(SetRenderState)(d,D3DRS_MULTISAMPLEMASK,0xffffffff));
    // Stage 0's fixed-function coordinate index and texture transform shaped
    // the TEXCOORD0 of the pre-transformed quad on the Preview backend (found
    // by the fixture's hostile state); with the vertex program bound they are
    // inert, and the twin path still needs them. Both go back through the block.
    STEP(call<SetStageFn>(SetTextureStageState)(d,0,D3DTSS_TEXCOORDINDEX,0));
    STEP(call<SetStageFn>(SetTextureStageState)(d,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
    for(UINT i=0;i<7;++i){
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_MINFILTER,D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_SRGBTEXTURE,FALSE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d,i,D3DSAMP_MAXMIPLEVEL,0));
    }
    D3DVIEWPORT9 viewport{0,0,w,h,0,1};STEP(call<SetViewportFn>(SetViewport)(d,&viewport));
#undef STEP
    return S_OK;
}
TemporalPass::~TemporalPass(){shutdown();}
void TemporalPass::invalidate() noexcept {history_.invalidate();diagnostics_.history_valid=false;++generation_;}
void TemporalPass::release_history() noexcept {
    invalidate();for(auto& p:color_surfaces_)drop(p);for(auto& p:depth_surfaces_)drop(p);
    for(auto& p:reactive_surfaces_)drop(p);
    for(auto& p:age_surfaces_)drop(p);
    for(auto& p:ages_)drop(p);
    for(auto& p:line_mask_surfaces_)drop(p);
    for(auto& p:line_masks_)drop(p);
    for(auto& p:box_surfaces_)drop(p);
    for(auto& p:boxes_)drop(p);
    for(auto& p:box_row_surfaces_)drop(p);
    for(auto& p:box_rows_)drop(p);
    drop(scratch_surface_);drop(staging_surface_);
    for(auto& p:colors_)drop(p);
    for(auto& p:depths_)drop(p);
    for(auto& p:reactive_)drop(p);
    drop(scratch_);drop(staging_);staging_format_=D3DFMT_UNKNOWN;
    reactive_policy_=ReactivePolicy::Unavailable;
    width_=height_=current_=0;
}
void TemporalPass::shutdown() noexcept {release_history();drop(block_);drop(decoder_);drop(resolve_);drop(snapshot_);drop(thin_);drop(age_);drop(far_);drop(far_camera_);drop(resolve16_);drop(thin16_);drop(age16_);drop(far16_);drop(far_camera16_);bilinear_history_=false;bilinear_history_reason_="not_initialized";drop(line_mask_camera_);drop(line_mask_depth_);drop(line_mask_camera_depth_);drop(line_mask_depth_thin_);drop(line_mask_camera_depth_thin_);drop(thin_box_);drop(thin_box_rows_);drop(thin_box_columns_);drop(far_camera_hold_);drop(thin_box_hold_);drop(thin_box_rows_hold_);drop(thin_box_columns_hold_);hold_history_=false;ps30_slots_=0;line_masks_failed_=false;line_masks_result_=S_OK;boxes_failed_=false;boxes_result_=S_OK;box_rows_failed_=false;box_rows_result_=S_OK;drop(line_mask_);mrt_age_=false;drop(sharpen_);drop(copy_);drop(quad_vs_);drop(quad_declaration_);device_=nullptr;vtable_=nullptr;render_targets_=streams_=0;diagnostics_.reset_pending=false;}
// Every owned texture and the state block must not exist across Reset; the
// compiled shaders survive it. Runs are refused until after_reset succeeds.
void TemporalPass::before_reset() noexcept {line_masks_failed_=false;line_masks_result_=S_OK;boxes_failed_=false;boxes_result_=S_OK;box_rows_failed_=false;box_rows_result_=S_OK;release_history();drop(block_);diagnostics_.reset_pending=device_!=nullptr;}
void TemporalPass::after_reset(HRESULT result) noexcept {invalidate();if(SUCCEEDED(result))diagnostics_.reset_pending=false;}
HRESULT TemporalPass::initialize(IDirect3DDevice9* d,const DWORD* decoder,const DWORD* resolve,void* const* native_vtable,const DWORD* sharpen,const DWORD* copy) noexcept {
    shutdown();diagnostics_={};snapshot_result_=S_FALSE;if(!d||!resolve)return E_INVALIDARG;
    device_=d;vtable_=native_vtable;quad_fvf_=quad_fvf_requested();
    D3DCAPS9 caps{};HRESULT hr=call<CapsFn>(GetDeviceCaps)(d,&caps);
    if(SUCCEEDED(hr)&&(caps.PixelShaderVersion<D3DPS_VERSION(3,0)||caps.VertexShaderVersion<D3DVS_VERSION(3,0)||!caps.NumSimultaneousRTs||caps.NumSimultaneousRTs>4||!caps.MaxStreams))hr=D3DERR_NOTAVAILABLE;
    if(FAILED(hr)){device_=nullptr;vtable_=nullptr;return hr;}
    render_targets_=caps.NumSimultaneousRTs;streams_=caps.MaxStreams;ps30_slots_=caps.MaxPixelShader30InstructionSlots;
    mrt_age_=caps.NumSimultaneousRTs>=2&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
    // S3: the 5-tap programs filter the FP16 colour and R32F mask histories; without that every slot takes the 16-tap words.
    bilinear_history_=SUCCEEDED(query_bilinear_history(caps));
    // The quad's vertex program and declaration survive Reset like the pixel programs.
    hr=call<CreateVsFn>(CreateVertexShader)(d,reinterpret_cast<const DWORD*>(quad_vertex_program()),&quad_vs_);
    if(SUCCEEDED(hr))hr=call<CreateDeclarationFn>(CreateVertexDeclaration)(d,quad_declaration,&quad_declaration_);
    if(SUCCEEDED(hr)&&decoder)hr=call<CreatePsFn>(CreatePixelShader)(d,decoder,&decoder_);
    if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(d,bilinear_history_?resolve:reinterpret_cast<const DWORD*>(temporal_resolve_taps16_program()),&resolve_);
    // The mask-snapshot modes are their own embedded program (resolve_snapshot.hlsl).
    // Optional: a device that refuses it keeps the resolve; only the mask policies that draw snapshots are refused at run.
    if(SUCCEEDED(hr)){snapshot_result_=call<CreatePsFn>(CreatePixelShader)(d,reinterpret_cast<const DWORD*>(temporal_resolve_snapshot_program()),&snapshot_);if(FAILED(snapshot_result_))drop(snapshot_);}
    if(SUCCEEDED(hr)&&sharpen)hr=call<CreatePsFn>(CreatePixelShader)(d,sharpen,&sharpen_);
    if(SUCCEEDED(hr)&&copy)hr=call<CreatePsFn>(CreatePixelShader)(d,copy,&copy_);
    if(FAILED(hr))shutdown();
    return hr;
}
// S3: the 5-tap programs' LINEAR samplers read the FP16 colour history (s11) and the R32F mask history (s12). Documented
// D3D9 capability checks only: the min / mag filter caps and D3DUSAGE_QUERY_FILTER of both formats against the adapter's
// current display format (a format without filter support samples LINEAR undefined, so a refusal selects the 16-tap
// programs, which point-sample everything).
HRESULT TemporalPass::query_bilinear_history(const D3DCAPS9& caps) noexcept {
    constexpr DWORD filters=D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFLINEAR;
    if((caps.TextureFilterCaps&filters)!=filters){bilinear_history_reason_="filter_caps";return D3DERR_NOTAVAILABLE;}
    IDirect3D9* api=nullptr;D3DDEVICE_CREATION_PARAMETERS creation{};D3DDISPLAYMODE mode{};
    HRESULT hr=call<GetD3DFn>(GetDirect3D)(device_,&api);
    if(SUCCEEDED(hr)&&!api)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=call<GetCreationFn>(GetCreationParameters)(device_,&creation);
    if(SUCCEEDED(hr))hr=call<GetDisplayModeFn>(GetDisplayMode)(device_,0,&mode);
    if(FAILED(hr)){drop(api);bilinear_history_reason_="adapter_query";return hr;}
    bilinear_history_reason_="ok";
    if(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,D3DFMT_A16B16G16R16F)!=D3D_OK)bilinear_history_reason_="fp16_filter";
    else if(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,D3DFMT_R32F)!=D3D_OK)bilinear_history_reason_="r32f_filter";
    drop(api);
    return std::strcmp(bilinear_history_reason_,"ok")==0?S_OK:D3DERR_NOTAVAILABLE;
}
HRESULT TemporalPass::configure_history_taps(unsigned taps) noexcept {
    if(taps!=5&&taps!=16)return E_INVALIDARG;
    history_taps_=taps;create_taps16(nullptr);
    return S_OK;
}
// S3: the 16-tap twins of the programs created so far, only while 16 taps are configured on a device that draws the
// 5-tap programs (without the filter caps the primary slots already hold the 16-tap words), so a default session holds
// no extra program. Each twin is optional: a refusal leaves that configuration on 5 taps (Diagnostics::history_taps).
void TemporalPass::create_taps16(const DWORD* far_reference) noexcept {
    if(!device_||!bilinear_history_||history_taps_!=16)return;
    auto make=[&](const DWORD* words,IDirect3DPixelShader9** out){if(!*out&&FAILED(call<CreatePsFn>(CreatePixelShader)(device_,words,out)))drop(*out);};
    auto words=[](const auto& program){return reinterpret_cast<const DWORD*>(program);};
    if(resolve_)make(words(temporal_resolve_taps16_program()),&resolve16_);
    if(thin_)make(words(temporal_resolve_thin_taps16_program()),&thin16_);
    if(age_)make(words(temporal_resolve_age_taps16_program()),&age16_);
    if(far_)make(far_reference?far_reference:words(temporal_resolve_far_taps16_program()),&far16_);
    if(far_camera_)make(words(temporal_resolve_far_camera_taps16_program()),&far_camera16_);
}
HRESULT TemporalPass::configure_flicker() noexcept {
    if(!device_||!resolve_)return E_FAIL;
    if(thin_)return S_OK;
    auto make=[&](const std::uint32_t* words,IDirect3DPixelShader9** out){return call<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words),out);};
    HRESULT hr=make(bilinear_history_?temporal_resolve_thin_program():temporal_resolve_thin_taps16_program(),&thin_);
    if(FAILED(hr)){drop(thin_);return hr;}
    // The age variant is optional on top: a refusal leaves the thin clip usable.
    if(mrt_age_&&FAILED(make(bilinear_history_?temporal_resolve_age_program():temporal_resolve_age_taps16_program(),&age_)))drop(age_);
    create_taps16(nullptr);
    return S_OK;
}
HRESULT TemporalPass::configure_far(const DWORD* reference_program) noexcept {
    if(!device_||!resolve_||!mrt_age_)return E_FAIL;
    if(far_)return S_OK;
    auto make=[&](const std::uint32_t* words,IDirect3DPixelShader9** out){return call<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words),out);};
    const bool own_mask=!line_mask_;
    HRESULT hr=line_mask_?S_OK:make(temporal_line_mask_program(),&line_mask_);
    if(SUCCEEDED(hr))hr=reference_program?call<CreatePsFn>(CreatePixelShader)(device_,reference_program,&far_):make(bilinear_history_?temporal_resolve_far_program():temporal_resolve_far_taps16_program(),&far_);
    if(FAILED(hr)){drop(far_);if(own_mask)drop(line_mask_);return hr;}
    // The camera-gate programs (section 32.1) are optional on top: a refusal leaves the thin region with its screen-speed gate
    // and a run asking for the camera gate is refused (camera_gate_available()).
    HRESULT camera=make(temporal_line_mask_camera_program(),&line_mask_camera_);
    if(SUCCEEDED(camera))camera=make(bilinear_history_?temporal_resolve_far_camera_program():temporal_resolve_far_camera_taps16_program(),&far_camera_);
    if(SUCCEEDED(camera))camera=make(temporal_thin_box_program(),&thin_box_);
    if(FAILED(camera)){drop(line_mask_camera_);drop(far_camera_);drop(thin_box_);}
    // S1 (taa-high-resolution.md): the depth-folding first draws, optional; a refusal keeps the copy draw for that program.
    if(!line_mask_depth_&&FAILED(make(temporal_line_mask_depth_program(),&line_mask_depth_)))drop(line_mask_depth_);
    if(line_mask_camera_&&!line_mask_camera_depth_&&FAILED(make(temporal_line_mask_camera_depth_program(),&line_mask_camera_depth_)))drop(line_mask_camera_depth_);
    create_taps16(reference_program); // a fixture's reference program stands for both tap counts
    return hr;
}
HRESULT TemporalPass::configure_sentinel() noexcept {
    if(!camera_gate_available())return E_FAIL;
    auto make=[&](const std::uint32_t* words,IDirect3DPixelShader9** out){return call<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words),out);};
    HRESULT hr=S_OK;
    if(!(thin_box_rows_&&thin_box_columns_)){
        hr=make(temporal_thin_box_rows_program(),&thin_box_rows_);
        if(SUCCEEDED(hr))hr=make(temporal_thin_box_columns_program(),&thin_box_columns_);
        if(FAILED(hr)){drop(thin_box_rows_);drop(thin_box_columns_);return hr;}
    }
    // A': the separable box's hold twins when the region hold was configured first (optional: without them a hold run with
    // the stabiliser on is refused, region_hold_sentinel_available()).
    if(far_camera_hold_&&!(thin_box_rows_hold_&&thin_box_columns_hold_)){
        HRESULT twins=make(temporal_thin_box_rows_hold_program(),&thin_box_rows_hold_);
        if(SUCCEEDED(twins))twins=make(temporal_thin_box_columns_hold_program(),&thin_box_columns_hold_);
        if(FAILED(twins)){drop(thin_box_rows_hold_);drop(thin_box_columns_hold_);}
    }
    return hr;
}
HRESULT TemporalPass::configure_thin_vote() noexcept {
    if(!device_)return E_FAIL;
    auto make=[&](const std::uint32_t* words,IDirect3DPixelShader9** out){return call<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words),out);};
    HRESULT hr=S_OK;
    if(!line_mask_depth_thin_&&FAILED(hr=make(temporal_line_mask_depth_thin_program(),&line_mask_depth_thin_)))drop(line_mask_depth_thin_);
    HRESULT camera=S_OK;
    if(!line_mask_camera_depth_thin_&&FAILED(camera=make(temporal_line_mask_camera_depth_thin_program(),&line_mask_camera_depth_thin_)))drop(line_mask_camera_depth_thin_);
    return FAILED(hr)?hr:camera;
}
HRESULT TemporalPass::configure_region_hold() noexcept {
    if(!camera_gate_available())return E_FAIL;
    if(!bilinear_history_)return D3DERR_NOTAVAILABLE; // the hold program has no 16-tap form
    auto make=[&](const std::uint32_t* words,IDirect3DPixelShader9** out){return call<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words),out);};
    HRESULT hr=S_OK;
    if(!(far_camera_hold_&&thin_box_hold_)){
        hr=make(temporal_resolve_far_camera_hold_program(),&far_camera_hold_);
        if(SUCCEEDED(hr))hr=make(temporal_thin_box_hold_program(),&thin_box_hold_);
        if(FAILED(hr)){drop(far_camera_hold_);drop(thin_box_hold_);return hr;}
    }
    if(thin_box_rows_&&thin_box_columns_&&!(thin_box_rows_hold_&&thin_box_columns_hold_)){
        HRESULT twins=make(temporal_thin_box_rows_hold_program(),&thin_box_rows_hold_);
        if(SUCCEEDED(twins))twins=make(temporal_thin_box_columns_hold_program(),&thin_box_columns_hold_);
        if(FAILED(twins)){drop(thin_box_rows_hold_);drop(thin_box_columns_hold_);}
    }
    return hr;
}
HRESULT TemporalPass::allocate(UINT w,UINT h,bool reactive,bool age) noexcept {
    if(width_==w&&height_==h&&bool(reactive_[0])==reactive&&bool(ages_[0])==age)return S_OK;
    release_history();HRESULT hr=S_OK;
    for(UINT i=0;i<2&&SUCCEEDED(hr);++i){
        hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&colors_[i],nullptr);
        if(SUCCEEDED(hr))hr=colors_[i]->GetSurfaceLevel(0,&color_surfaces_[i]);
        if(SUCCEEDED(hr))hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depths_[i],nullptr);
        if(SUCCEEDED(hr))hr=depths_[i]->GetSurfaceLevel(0,&depth_surfaces_[i]);
        if(SUCCEEDED(hr)&&reactive)hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&reactive_[i],nullptr);
        if(SUCCEEDED(hr)&&reactive)hr=reactive_[i]->GetSurfaceLevel(0,&reactive_surfaces_[i]);
        if(SUCCEEDED(hr)&&age)hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&ages_[i],nullptr);
        if(SUCCEEDED(hr)&&age)hr=ages_[i]->GetSurfaceLevel(0,&age_surfaces_[i]);
    }
    if(FAILED(hr)){release_history();return hr;}width_=w;height_=h;return S_OK;
}
HRESULT TemporalPass::ensure_scratch() noexcept {
    if(scratch_)return S_OK;
    HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,width_,height_,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&scratch_,nullptr);
    if(SUCCEEDED(hr))hr=scratch_->GetSurfaceLevel(0,&scratch_surface_);
    if(FAILED(hr)){drop(scratch_surface_);drop(scratch_);}
    return hr;
}
// Draw mode: the same-format render-target texture the 8-bit input is copied
// into (a same-format RT-to-RT StretchRect, what the game's own bloom copy
// does) and the identity draw samples into the FP16 scratch. Re-created when
// the input's format changes; default pool, released with the histories.
HRESULT TemporalPass::ensure_staging(D3DFORMAT format) noexcept {
    if(staging_&&staging_format_==format)return S_OK;
    drop(staging_surface_);drop(staging_);staging_format_=D3DFMT_UNKNOWN;
    HRESULT hr=call<CreateTextureFn>(CreateTexture)(device_,width_,height_,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&staging_,nullptr);
    if(SUCCEEDED(hr))hr=staging_->GetSurfaceLevel(0,&staging_surface_);
    if(FAILED(hr)){drop(staging_surface_);drop(staging_);}else staging_format_=format;
    return hr;
}
HRESULT TemporalPass::ensure_line_masks(bool both) noexcept {
    HRESULT hr=S_OK;
    for(UINT i=0;i<(both?2u:1u)&&SUCCEEDED(hr);++i){
        if(line_masks_[i])continue;
        hr=call<CreateTextureFn>(CreateTexture)(device_,width_,height_,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&line_masks_[i],nullptr);
        if(SUCCEEDED(hr))hr=line_masks_[i]->GetSurfaceLevel(0,&line_mask_surfaces_[i]);
    }
    if(FAILED(hr)){for(auto& p:line_mask_surfaces_)drop(p);for(auto& p:line_masks_)drop(p);}
    return hr;
}
HRESULT TemporalPass::ensure_boxes() noexcept {
    if(boxes_[1])return S_OK;
    HRESULT hr=S_OK;
    for(UINT i=0;i<2&&SUCCEEDED(hr);++i){
        hr=call<CreateTextureFn>(CreateTexture)(device_,width_,height_,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&boxes_[i],nullptr);
        if(SUCCEEDED(hr))hr=boxes_[i]->GetSurfaceLevel(0,&box_surfaces_[i]);
    }
    if(FAILED(hr)){for(auto& p:box_surfaces_)drop(p);for(auto& p:boxes_)drop(p);}
    return hr;
}
HRESULT TemporalPass::ensure_box_rows() noexcept {
    if(box_rows_[1])return S_OK;
    HRESULT hr=S_OK;
    for(UINT i=0;i<2&&SUCCEEDED(hr);++i){
        hr=call<CreateTextureFn>(CreateTexture)(device_,width_,height_,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&box_rows_[i],nullptr);
        if(SUCCEEDED(hr))hr=box_rows_[i]->GetSurfaceLevel(0,&box_row_surfaces_[i]);
    }
    if(FAILED(hr)){for(auto& p:box_row_surfaces_)drop(p);for(auto& p:box_rows_)drop(p);}
    return hr;
}
HRESULT TemporalPass::ensure_block() noexcept {
    if(block_)return S_OK;
    const HRESULT hr=call<CreateBlockFn>(CreateStateBlock)(device_,D3DSBT_ALL,&block_);
    if(FAILED(hr))drop(block_);
    return hr;
}
HRESULT TemporalPass::run(const FrameInputs& in,Output* out) noexcept {
    if(out)*out={};
    diagnostics_.operation=diagnostics_.restoration=S_OK;diagnostics_.depth_folded=false;diagnostics_.depth_fold_reason="not_run";diagnostics_.history_taps=0;diagnostics_.region_hold=false;diagnostics_.thin_vote=false;diagnostics_.thin_vote_reason="not_run";
    // Phase timing (Diagnostics::ticks_*): QPC pairs only, no device call changes.
    diagnostics_.timed=timing_;
    diagnostics_.ticks_capture=diagnostics_.ticks_copy_color=diagnostics_.ticks_copy_depth=diagnostics_.ticks_draw=diagnostics_.ticks_apply=0;
    auto fail=[&](HRESULT hr){invalidate();diagnostics_.operation=hr;return hr;};
    const bool supplemental=in.reactive_policy==ReactivePolicy::SupplementalMaskWithDepthSentinel;
    const bool mask=in.reactive_policy==ReactivePolicy::RequiredMask||supplemental;
    const bool sentinel=in.reactive_policy==ReactivePolicy::DerivedFromDepthSentinel||supplemental;
    const bool draw_copy=in.color_surface&&copy_by_draw_;
    const bool thin_region=in.thin_region_weight>0;
    const bool far_requested=in.far_weight>0||in.far_filter>0||thin_region; // everything the far program carries
    const bool camera_requested=thin_region&&in.thin_region_camera_gate; // section 32.1: the camera-relative gate programs and the box targets
    const bool sentinel_requested=camera_requested&&in.sentinel_strength>0; // the sentinel stabiliser: ignored without the camera gate
    const bool hold_requested=camera_requested&&in.thin_region_hold; // A': ignored without the camera gate
    const bool adaptive=in.adaptive_weight>0,flicker=in.thin_clip>0||adaptive||in.alpha_history;
    bool far_on=far_requested,aged=adaptive||far_on;
    if(!out||!device_||!resolve_||(mask&&!snapshot_)||!quad_vs_||!quad_declaration_||diagnostics_.reset_pending||!in.width||!in.height||in.caller_stateblock_recording||!in.caller_queries_idle||(draw_copy&&!copy_)||
        (in.motion_policy!=MotionPolicy::KnownCameraOnly&&in.motion_policy!=MotionPolicy::PerPixel)||
        (in.reactive_policy!=ReactivePolicy::Unavailable&&in.reactive_policy!=ReactivePolicy::KnownNonReactive&&
         in.reactive_policy!=ReactivePolicy::RequiredMask&&!sentinel)||
        (!mask&&in.reactive)||
        bool(in.color)==bool(in.color_surface)||bool(in.depth_snapshot)==bool(in.current_depth)||
        (in.depth_snapshot&&!decoder_)||(sentinel&&!in.current_depth)||
        !x3::temporal::valid_sharpen(in.sharpen)||(in.sharpen>0&&(!in.color_surface||!sharpen_))||
        !x3::temporal::valid_far_weight(in.far_weight,in.weight)||!x3::temporal::valid_current_filter(in.far_filter)||!std::isfinite(in.far_d0)||!std::isfinite(in.far_inv)||in.far_inv<0||
        !x3::temporal::valid_far_weight(in.thin_region_weight,in.weight)||!x3::temporal::valid_thin_clip(in.thin_region_relax)||
        (thin_region&&(!std::isfinite(in.thin_region_emissive)||in.thin_region_emissive<0))||
        (far_requested&&(in.thin_clip>0||!x3::temporal::valid_far_speed_gate(in.far_speed_lo,in.far_speed_hi)||!far_available()||in.motion_policy!=MotionPolicy::PerPixel||adaptive))||
        (camera_requested&&!camera_gate_available())||
        (hold_requested&&(!region_hold_available()||(sentinel_requested&&!region_hold_sentinel_available())||in.thin_region_hold_frames<1||in.thin_region_hold_frames>64))||
        (camera_requested&&!x3::temporal::valid_thin_clip(in.sentinel_strength))||(sentinel_requested&&(!std::isfinite(in.sentinel_emitter)||in.sentinel_emitter<0||!sentinel_available()))||
        !x3::temporal::valid_thin_clip(in.thin_clip)||!x3::temporal::valid_adaptive_weight(in.adaptive_weight,in.adaptive_lo,in.adaptive_hi,in.weight)||
        (flicker&&!far_requested&&!flicker_available())||(adaptive&&(!(in.thin_clip>0)||!age_available()))||(in.alpha_history&&!in.color))return fail(E_INVALIDARG);
    for(UINT i=0;i<2;++i){
        if(ages_[i]&&(in.color==ages_[i]||in.depth_snapshot==ages_[i]||in.current_depth==ages_[i]||in.motion==ages_[i]||in.reactive==ages_[i]))return fail(E_INVALIDARG);
        for(auto* owned:{colors_[i],depths_[i],reactive_[i],boxes_[i],box_rows_[i],scratch_,staging_})
            if(owned&&(in.color==owned||in.depth_snapshot==owned||in.current_depth==owned||in.motion==owned||in.reactive==owned))return fail(E_INVALIDARG);
        if(in.color_surface&&(in.color_surface==color_surfaces_[i]||in.color_surface==scratch_surface_||in.color_surface==staging_surface_))return fail(E_INVALIDARG);
    }
    D3DFORMAT surface_format=D3DFMT_UNKNOWN, depth_format=D3DFMT_UNKNOWN;
    HRESULT hr=in.color?texture_input(device_,in.color,in.width,in.height,D3DFMT_A16B16G16R16F):surface_input(device_,in.color_surface,in.width,in.height,&surface_format);
    if(SUCCEEDED(hr))hr=in.current_depth?texture_input(device_,in.current_depth,in.width,in.height,D3DFMT_R32F,&depth_format):texture_input(device_,in.depth_snapshot,in.width,in.height,D3DFMT_D24X8);
    if(SUCCEEDED(hr)&&in.motion_policy==MotionPolicy::PerPixel)hr=texture_input(device_,in.motion,in.width,in.height,D3DFMT_A32B32G32R32F);
    if(SUCCEEDED(hr)&&mask)hr=texture_input(device_,in.reactive,in.width,in.height,supplemental?D3DFMT_A16B16G16R16F:D3DFMT_R32F);
    if(FAILED(hr))return fail(hr);
    const bool depth_draw=depth_format==D3DFMT_G32R32F||depth_format==D3DFMT_A32B32G32R32F; // the lane's RT2: the point-sampled .r copy; R32F StretchRects, the D24X8 snapshot decodes
    if(depth_draw&&!copy_)return fail(E_INVALIDARG);
    hr=allocate(in.width,in.height,mask,aged);if(FAILED(hr))return fail(hr);
    // The mask targets of the far stabiliser / thin region: pure allocation, before any state is touched. A failure
    // that is not a lost device turns them off for the session (line_masks_failed()); this and later runs
    // proceed without them. The age pair a far run allocated stays (unused), so the history survives the fallback.
    // A' draws the tests target alone ([0]); the 16-tap programs have no hold form and keep the dilations.
    const bool hold_wanted=hold_requested&&history_taps_!=16&&!boxes_failed_;
    if(far_on&&!line_masks_failed_){const HRESULT masks=ensure_line_masks(!hold_wanted);if(lost(masks))return fail(masks);if(FAILED(masks)){line_masks_failed_=true;line_masks_result_=masks;}}
    if(line_masks_failed_){far_on=false;aged=adaptive;}
    // The box targets of the camera gate: same policy as the mask targets, but the fallback is the screen-speed gate, not the plain resolve.
    bool camera=camera_requested&&far_on&&!boxes_failed_;
    if(camera){const HRESULT boxes=ensure_boxes();if(lost(boxes))return fail(boxes);if(FAILED(boxes)){boxes_failed_=true;boxes_result_=boxes;camera=false;}}
    // A run that does not use the camera gate returns the box pair (15.7 MiB at 1280x768). The gate mode is a session setting, so
    // this fires once on a configuration change, never per frame; nothing is bound yet, and the histories are untouched.
    else if(boxes_[0]){for(auto& p:box_surfaces_)drop(p);for(auto& p:boxes_)drop(p);}
    const bool hold=hold_wanted&&camera;
    // A hold run that lost its box targets (or a run without the hold) falls back to the screen gate, which needs the second mask target.
    if(far_on&&!hold&&!line_masks_[1]){const HRESULT masks=ensure_line_masks(true);if(lost(masks))return fail(masks);if(FAILED(masks)){line_masks_failed_=true;line_masks_result_=masks;far_on=false;aged=adaptive;camera=false;}}
    // A hold run returns the second mask target (a configuration change, never per frame); nothing is bound yet.
    else if(hold&&line_masks_[1]){drop(line_mask_surfaces_[1]);drop(line_masks_[1]);}
    // The row targets of the sentinel stabiliser's separable box: a failure that is not a lost device turns the stabiliser
    // off for the session and the camera gate carries on with the 49-tap box; released by the first run without it.
    bool stabilise=camera&&sentinel_requested&&!box_rows_failed_;
    if(stabilise){const HRESULT rows=ensure_box_rows();if(lost(rows))return fail(rows);if(FAILED(rows)){box_rows_failed_=true;box_rows_result_=rows;stabilise=false;}}
    else if(box_rows_[0]){for(auto& p:box_row_surfaces_)drop(p);for(auto& p:box_rows_)drop(p);}
    // S1 (docs/architecture/taa-high-resolution.md): on a two- or four-channel current depth the mask chain's first draw reads
    // it at s1 itself and writes depths_[next] as COLOR1 (R32F beside the A8R8G8B8 mask: two targets and
    // MRTINDEPENDENTBITDEPTHS, both in mrt_age_); the copy draw below does not run. Every later reader of depths_[next]
    // (the dilations' fallback at s6, the box columns, the resolve, the next frame's history) comes after that draw.
    IDirect3DPixelShader9* const plain_fold=depth_draw&&far_on&&mrt_age_&&render_targets_>=2?(camera?line_mask_camera_depth_:line_mask_depth_):nullptr;
    // Thin vote: the twin of the same fold program, on a four-channel depth with the thin region on (the vote sets the flag).
    IDirect3DPixelShader9* const thin_fold=plain_fold&&in.thin_vote&&thin_region&&depth_format==D3DFMT_A32B32G32R32F?(camera?line_mask_camera_depth_thin_:line_mask_depth_thin_):nullptr;
    IDirect3DPixelShader9* const fold_program=thin_fold?thin_fold:plain_fold;
    diagnostics_.thin_vote=thin_fold!=nullptr;
    diagnostics_.thin_vote_reason=thin_fold?"vote":!in.thin_vote?"not_requested":!thin_region?"thin_region_off":!plain_fold?"no_fold":
        depth_format!=D3DFMT_A32B32G32R32F?"two_channel_depth":"no_twin_program";
    const bool fold=fold_program!=nullptr;
    diagnostics_.depth_fold_reason=fold?"lane_mrt":!depth_draw?(in.current_depth?"r32f_depth":"d24_decode"):!far_on?"far_off":!(mrt_age_&&render_targets_>=2)?"mrt_caps":"program";
    hr=ensure_block();if(FAILED(hr))return fail(hr);
    history_.begin(in.width,in.height,in.epoch);
    if(in.camera_cut||in.cut||!in.history_allowed||in.reactive_policy==ReactivePolicy::Unavailable||
       in.reactive_policy!=reactive_policy_||(hold_history_&&!hold))invalidate(); // the other age programs read whole counts
    x3::temporal::ResolveConstants constants{};std::copy(in.rejection,in.rejection+4,constants.rejection);
    if(!x3::temporal::prepare(constants,history_,in.clip_to_previous,in.current_jitter[0],in.current_jitter[1],
        in.previous_jitter[0],in.previous_jitter[1],in.weight,in.motion_policy==MotionPolicy::PerPixel,
        mask,sentinel,sentinel&&in.sentinel_camera,in.luminance_k,in.sentinel_strict_sky))return fail(E_INVALIDARG);
    const float strict_sky_term=constants.options[2]; // the resolve's c7.z; the snapshot and mask draws below set their own mode and put it back
    // c5.y (the previous jitter's lane, unread by every resolve program) = the band threshold squared (px^2): seta-motion.md section 4.
    if(!(std::isfinite(in.sky_history_band_px)&&in.sky_history_band_px>=1.f&&in.sky_history_band_px<=16.f))return fail(E_INVALIDARG);
    constants.history[1]=in.sky_history_band_px*in.sky_history_band_px;
    if(!x3::temporal::valid_sky_history_exit(in.sky_history_exit_px,in.sky_history_band_px))return fail(E_INVALIDARG);
    if(!x3::temporal::valid_motion_weight(in.motion_weight,in.motion_weight_v0,in.motion_weight_v1))return fail(E_INVALIDARG);
    constants.luminance[2]=in.alpha_history?1.f:0.f; // read by the flicker variants only
    constants.luminance[3]=far_on?in.far_filter:0.f; // A of the masked filter: far variants only
    // c24 and, for the age programs, c25 (the exit floor squared under strict, else off: seta-sky-hull-share-decay.md) as one block.
    float flicker_constants[8]{};x3::temporal::prepare_flicker(flicker_constants,in.thin_clip,in.adaptive_weight,in.adaptive_lo,in.adaptive_hi);
    x3::temporal::prepare_exit(flicker_constants+4,in.sky_history_exit_px,strict_sky_term>0.f);
    // c25.yzw: the motion history weight's A, B, F (taa-motion-history-weight.md), 0, 1, 1 when off: the age programs' cap is then exactly 1.
    x3::temporal::prepare_motion_weight(flicker_constants+4,in.motion_weight,in.motion_weight_v0,in.motion_weight_v1);
    // Far variant: c24.yzw = W_FAR (the base weight when that component is off; its gate channel is 0 then), speed gate far_speed_lo .. far_speed_hi px/frame.
    // Far program: c24.x = clip relaxation of the thin region, c24.y = W_FAR (the base weight when off), c24.zw the shared speed gate;
    // c5.x (unread by every resolve until now) = the thin-region weight (the base weight when off).
    const bool thin_on=far_on&&thin_region;
    if(far_on){flicker_constants[0]=thin_on?in.thin_region_relax:0.f;flicker_constants[1]=in.far_weight>0?in.far_weight:in.weight;flicker_constants[2]=in.far_speed_lo;flicker_constants[3]=1.f/(in.far_speed_hi-in.far_speed_lo);
        constants.history[0]=thin_on?in.thin_region_weight:in.weight;}
    const float far_constants[4]={in.far_d0,far_on?in.far_inv:0.f,far_on&&in.far_filter>0?1.f:0.f,far_on&&in.far_weight>0?1.f:0.f};
    const float thin_constants[4]={stabilise?in.sentinel_strength:0.f,thin_on?1.f:0.f,in.far_speed_lo,far_on?1.f/(in.far_speed_hi-in.far_speed_lo):0.f};
    // Mask tests draw only (thin-glow-lines.md 8.3 R3): c10.x = E of the emissive vote, uploaded on every mask draw so no
    // stale caller constant can open it; 0 (off, or the thin region off) leaves the mask bit for bit and takes no scene tap.
    const bool emissive_vote=thin_on&&in.thin_region_emissive>0;
    const float emissive_constants[4]={emissive_vote?in.thin_region_emissive:0.f,0.f,0.f,0.f};
    UINT final_mask=1; // which owned mask target the resolve reads
    // A' (hold program only): c11 = S of the sentinel stabiliser (thinGate.x of the mask's composition), the far components'
    // scales (farGate.zw), the hold length in frames.
    const float hold_constants[4]={thin_constants[0],far_constants[2],far_constants[3],float(in.thin_region_hold_frames)};
    const bool thin_bound=flicker&&thin_; // after a mask fallback of a far run the thin variants may not exist: plain then
    // S3: the 16-tap twin when asked for and created; without the filter caps the 5-tap slots hold the 16-tap words.
    IDirect3DPixelShader9* const program16=history_taps_==16?(far_on?(camera?far_camera16_:far16_):aged?age16_:thin_bound?thin16_:resolve16_):nullptr;
    IDirect3DPixelShader9* const program=program16?program16:far_on?(camera?(hold?far_camera_hold_:far_camera_):far_):aged?age_:thin_bound?thin_:resolve_;
    const bool bilinear=bilinear_history_&&!program16; // the 5-tap program: its LINEAR samplers s11 / s12 are bound for the draw
    const bool used=history_.valid&&in.weight>0;
    auto stamp=[&]()->std::uint64_t{if(!timing_)return 0;LARGE_INTEGER t{};QueryPerformanceCounter(&t);return std::uint64_t(t.QuadPart);};
    std::uint64_t mark=stamp();
    SavedState saved(*this,block_,render_targets_);hr=saved.capture();
    diagnostics_.ticks_capture=stamp()-mark;
    if(FAILED(hr))return fail(hr);
    const UINT next=current_^1;
    bool own_scene=false;
    std::uint64_t depth_draw_ticks=0;
    mark=stamp();
    hr=normalize(in.width,in.height);
    diagnostics_.ticks_draw=stamp()-mark;
    auto step=[&](HRESULT value){hr=value;return SUCCEEDED(hr);};
    D d=device_;
    // Copies touch no device state; they run after normalize so the scratch and
    // the next depth history are bound nowhere. StretchRect is legal inside or
    // outside a scene. No sRGB flag is set on any sampler or target, so the
    // 8-bit copy is a plain UNORM-to-FP16 conversion of the linear-encoded data.
    // Draw mode (configure_copy(true)): the format conversion is not asked of
    // StretchRect; the input is copied same-format into the staging texture
    // and the identity draw inside the scene bracket below converts it. An
    // FP16 texture input (in.color) takes neither copy nor scratch.
    // --gpu-sync-timing sub-passes (configure_sync_timing): taa_copy .. taa_display below, each begin paired with an
    // unconditional end (an end without its begin records nothing); off, one null branch each.
    if(sync_marks_&&SUCCEEDED(hr))sync_marks_->begin(gpu_sync_timing::TaaCopy);
    mark=stamp();
    if(SUCCEEDED(hr)&&in.color_surface&&step(ensure_scratch())){
        if(draw_copy){if(step(ensure_staging(surface_format)))hr=call<StretchFn>(StretchRect)(d,in.color_surface,nullptr,staging_surface_,nullptr,D3DTEXF_POINT);}
        else hr=call<StretchFn>(StretchRect)(d,in.color_surface,nullptr,scratch_surface_,nullptr,D3DTEXF_POINT);
    }
    diagnostics_.ticks_copy_color=stamp()-mark;
    mark=stamp();
    if(SUCCEEDED(hr)&&in.current_depth&&!depth_draw){
        IDirect3DSurface9* source=nullptr;
        if(step(in.current_depth->GetSurfaceLevel(0,&source)))hr=call<StretchFn>(StretchRect)(d,source,nullptr,depth_surfaces_[next],nullptr,D3DTEXF_POINT);
        drop(source);
    }
    diagnostics_.ticks_copy_depth=stamp()-mark;
    mark=stamp();
    if(SUCCEEDED(hr)&&!in.caller_scene_open){hr=call<SceneFn>(BeginScene)(d);own_scene=SUCCEEDED(hr);}
    // Enhanced RT2 retains R32F histories. The identity program point-samples
    // the two- or four-channel source; R32F stores only .r, exactly preserving
    // sentinels. Never request an unsupported G32R32F/A32B32G32R32F -> R32F
    // StretchRect conversion.
    if(SUCCEEDED(hr)&&depth_draw&&!fold){
        const auto depth_mark=stamp();
        if(step(call<SetRtFn>(SetRenderTarget)(d,0,depth_surfaces_[next]))&&
           step(call<SetPsFn>(SetPixelShader)(d,copy_))&&
           step(call<SetTextureFn>(SetTexture)(d,0,in.current_depth)))hr=quad(in.width,in.height);
        depth_draw_ticks=stamp()-depth_mark;
        diagnostics_.ticks_copy_depth+=depth_draw_ticks;
    }
    // Draw mode: the identity program converts the staged 8-bit copy into the FP16 scratch.
    if(SUCCEEDED(hr)&&draw_copy&&step(call<SetRtFn>(SetRenderTarget)(d,0,scratch_surface_))&&
        step(call<SetPsFn>(SetPixelShader)(d,copy_))&&step(call<SetTextureFn>(SetTexture)(d,0,staging_)))hr=quad(in.width,in.height);
    if(SUCCEEDED(hr)&&in.depth_snapshot&&step(call<SetRtFn>(SetRenderTarget)(d,0,depth_surfaces_[next]))&&
        step(call<SetPsFn>(SetPixelShader)(d,decoder_))&&step(call<SetTextureFn>(SetTexture)(d,0,in.depth_snapshot)))hr=quad(in.width,in.height);
    // Supplemental coverage must protect the current 3x3 color statistics too.
    // Reuse the owned snapshot draw before resolve, with one-pixel expansion;
    // the previous history is already canonical/expanded and stays untouched.
    if(SUCCEEDED(hr)&&supplemental){
        constants.options[2]=2;
        if(step(call<SetRtFn>(SetRenderTarget)(d,0,reactive_surfaces_[next]))&&
           step(call<SetPsFn>(SetPixelShader)(d,snapshot_))&&
           step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,4,constants.size_jitter,1))&&
           step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,7,constants.options,1))&&
           step(call<SetTextureFn>(SetTexture)(d,5,in.reactive)))hr=quad(in.width,in.height);
        constants.options[2]=strict_sky_term;
    }
    if(sync_marks_){sync_marks_->end(gpu_sync_timing::TaaCopy);if(SUCCEEDED(hr)&&far_on)sync_marks_->begin(gpu_sync_timing::TaaMask);}
    // Far stabiliser / thin region: the mask of the current depth (now complete in depths_[next]),
    // bound at s8 for the resolve (point, clamp, single level; the block restores the sampler).
    // c7.z is the mask program's mode; the resolve's c7 is uploaded again below.
    if(SUCCEEDED(hr)&&far_on){
        const float resolve_policy=constants.options[3];
        // Camera mask only (section 32.3): c8, the depth / translation term of its camera path; anything non-finite is the far-plane path.
        float parallax_constants[4]={in.camera_depth_parallax[0],in.camera_depth_parallax[1],in.camera_depth_parallax[2],in.camera_depth_parallax[3]};
        if(!std::isfinite(parallax_constants[0])||!std::isfinite(parallax_constants[1])||!std::isfinite(parallax_constants[2])||!std::isfinite(parallax_constants[3]))
            parallax_constants[0]=parallax_constants[1]=parallax_constants[2]=parallax_constants[3]=0.f;
        float lane_constants[4]={in.camera_lane_parallax[0],in.camera_lane_parallax[1],in.camera_lane_parallax[2],1.f};
        const bool lane=camera&&in.current_depth&&depth_format==D3DFMT_A32B32G32R32F&&in.camera_lane_parallax[3]==1.f&&std::isfinite(lane_constants[0])&&std::isfinite(lane_constants[1])&&std::isfinite(lane_constants[2]);
        if(!lane)lane_constants[0]=lane_constants[1]=lane_constants[2]=lane_constants[3]=0.f;
        // Thin region: the per-pixel tests, then the dilations; far stabiliser alone: one draw (mode 2) into the second target.
        // Thin region: tests -> [0], maxima along x -> [1], along y and composition -> [0]. Far stabiliser alone: mode 2 -> [1].
        // Region hold (A'): tests -> [0] only; the resolve composes.
        const UINT draws=thin_on?(hold?1:3):1;final_mask=thin_on?0:1;
        for(UINT pass=0;pass<draws&&SUCCEEDED(hr);++pass){
            // S1: the first draw reads the caller's depth itself and writes depths_[next] as RT1 (unbound again right after).
            const bool fold_draw=fold&&pass==0;
            const UINT target=!thin_on?1:(pass==1?1:0);IDirect3DTexture9* const source=pass==0?(fold?in.current_depth:depths_[next]):line_masks_[pass==1?0:1];
            constants.options[2]=!thin_on?2.f:pass==0?0.f:pass==1?1.f:3.f;constants.options[3]=0.f;
            // --gpu-sync-timing: one sub-pass per draw inside taa_mask (the tests or far-only draw, maxima along x, along y + composition).
            gpu_sync_timing::Span sync_span(sync_marks_,pass==0?unsigned(gpu_sync_timing::TaaMaskTests):pass==1?unsigned(gpu_sync_timing::TaaMaskX):unsigned(gpu_sync_timing::TaaMaskY));
            if(step(call<SetTextureFn>(SetTexture)(d,1,nullptr))&&step(call<SetRtFn>(SetRenderTarget)(d,0,line_mask_surfaces_[target]))&&
               (!fold_draw||(step(call<SetRsFn>(SetRenderState)(d,D3DRS_COLORWRITEENABLE1,15))&&step(call<SetRtFn>(SetRenderTarget)(d,1,depth_surfaces_[next]))))&&
               step(call<SetPsFn>(SetPixelShader)(d,fold_draw?fold_program:camera?line_mask_camera_:line_mask_))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,0,&constants.clip_to_previous[0][0],4))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,4,constants.size_jitter,1))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,5,far_constants,1))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,6,thin_constants,1))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,7,constants.options,1))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,10,emissive_constants,1))&&
               // The emissive vote reads this frame's scene at s0 (already point / clamp from normalize; the resolve rebinds it).
               (!emissive_vote||step(call<SetTextureFn>(SetTexture)(d,0,pass==0?(in.color?in.color:scratch_):nullptr)))&&
               (!camera||(step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,8,parallax_constants,1))&&step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,9,lane_constants,1))&&
                          step(call<SetTextureFn>(SetTexture)(d,5,lane&&pass==0&&!fold?in.current_depth:nullptr))))&& // s0..s6 are point / clamp already; the resolve rebinds s5; the folded draw reads .b at s1
               (!stabilise||step(call<SetTextureFn>(SetTexture)(d,6,pass==2?depths_[next]:nullptr)))&& // sentinel stabiliser: s6 serves only the fallback path (thin region off); with the thin region on the tests draw carries the class; the resolve rebinds s6
               step(call<SetTextureFn>(SetTexture)(d,4,in.motion_policy==MotionPolicy::PerPixel?in.motion:nullptr))&&
               step(call<SetTextureFn>(SetTexture)(d,1,source)))hr=quad(in.width,in.height);
            if(fold_draw&&!lost(hr)){const HRESULT unbind=call<SetRtFn>(SetRenderTarget)(d,1,nullptr);if(SUCCEEDED(hr))hr=unbind;}
            if(fold_draw&&SUCCEEDED(hr))diagnostics_.depth_folded=true;
        }
        constants.options[2]=strict_sky_term;constants.options[3]=resolve_policy;
        if(SUCCEEDED(hr)&&step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_MAGFILTER,D3DTEXF_POINT))&&
           step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
           step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_SRGBTEXTURE,FALSE))&&
           step(call<SetSamplerFn>(SetSamplerState)(d,8,D3DSAMP_MAXMIPLEVEL,0)))hr=call<SetTextureFn>(SetTexture)(d,8,line_masks_[final_mask]);
        if(sync_marks_){sync_marks_->end(gpu_sync_timing::TaaMask);if(SUCCEEDED(hr)&&camera)sync_marks_->begin(gpu_sync_timing::TaaBox);}
        // A': the box twins read the previous age target at s7 (point, clamp, single level; the block restores the sampler, the
        // resolve binds s7 again) for the region the resolve held last frame; without a valid history there is no region yet.
        if(SUCCEEDED(hr)&&hold&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MAGFILTER,D3DTEXF_POINT))&&
           step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
           step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_SRGBTEXTURE,FALSE))&&
           step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MAXMIPLEVEL,0)))hr=call<SetTextureFn>(SetTexture)(d,7,history_.valid?ages_[current_]:nullptr);
        // Camera gate: the 7x7 min / max box of the current colour (thin_box_ps.hlsl) into the two box targets (MRT, both
        // FP16), skipping every pixel the camera term did not open (the final mask at s8 decides, the same texel the resolve
        // reads). RT1 leaves the device again right after; the resolve binds the boxes at s9 / s10 once RT0 has moved on.
        // Sentinel stabiliser: the box covers most of the sky, so it runs separably (thin_box_rows_ps.hlsl into the row pair only where a
        // column reader opens the box, thin_box_columns_ps.hlsl into the box pair where the mask opens; the same bytes as the 49-tap program,
        // plus the emitter bound c23.x, which the sharpen uploads again for itself later). The resolve rebinds s0..s3.
        if(SUCCEEDED(hr)&&stabilise){
            const float emitter[4]={in.sentinel_emitter,0.f,0.f,0.f};
            if(step(call<SetRsFn>(SetRenderState)(d,D3DRS_COLORWRITEENABLE1,15))&&step(call<SetTextureFn>(SetTexture)(d,0,nullptr))&&
               step(call<SetTextureFn>(SetTexture)(d,2,nullptr))&&step(call<SetTextureFn>(SetTexture)(d,3,nullptr))&&
               step(call<SetRtFn>(SetRenderTarget)(d,0,box_row_surfaces_[0]))&&step(call<SetRtFn>(SetRenderTarget)(d,1,box_row_surfaces_[1]))&&
               step(call<SetPsFn>(SetPixelShader)(d,hold?thin_box_rows_hold_:thin_box_rows_))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,0,&constants.clip_to_previous[0][0],x3::temporal::kResolveRegisterCount))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,x3::temporal::kLuminanceRegister,constants.luminance,1))&&
               step(call<SetTextureFn>(SetTexture)(d,0,in.color?in.color:scratch_))&&step(quad(in.width,in.height))&&
               step(call<SetRtFn>(SetRenderTarget)(d,0,box_surfaces_[0]))&&step(call<SetRtFn>(SetRenderTarget)(d,1,box_surfaces_[1]))&&
               step(call<SetPsFn>(SetPixelShader)(d,hold?thin_box_columns_hold_:thin_box_columns_))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,x3::temporal::kSharpenRegister,emitter,1))&&
               step(call<SetTextureFn>(SetTexture)(d,1,depths_[next]))&&
               step(call<SetTextureFn>(SetTexture)(d,2,box_rows_[0]))&&step(call<SetTextureFn>(SetTexture)(d,3,box_rows_[1])))hr=quad(in.width,in.height);
            if(!lost(hr)){const HRESULT unbind=call<SetRtFn>(SetRenderTarget)(d,1,nullptr);if(SUCCEEDED(hr))hr=unbind;}
        }else if(SUCCEEDED(hr)&&camera){
            if(step(call<SetRsFn>(SetRenderState)(d,D3DRS_COLORWRITEENABLE1,15))&&step(call<SetTextureFn>(SetTexture)(d,0,nullptr))&&
               step(call<SetRtFn>(SetRenderTarget)(d,0,box_surfaces_[0]))&&step(call<SetRtFn>(SetRenderTarget)(d,1,box_surfaces_[1]))&&
               step(call<SetPsFn>(SetPixelShader)(d,hold?thin_box_hold_:thin_box_))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,0,&constants.clip_to_previous[0][0],x3::temporal::kResolveRegisterCount))&&
               step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,x3::temporal::kLuminanceRegister,constants.luminance,1))&&
               step(call<SetTextureFn>(SetTexture)(d,0,in.color?in.color:scratch_)))hr=quad(in.width,in.height);
            if(!lost(hr)){const HRESULT unbind=call<SetRtFn>(SetRenderTarget)(d,1,nullptr);if(SUCCEEDED(hr))hr=unbind;}
        }
        if(sync_marks_)sync_marks_->end(gpu_sync_timing::TaaBox);
    }
    if(sync_marks_&&SUCCEEDED(hr))sync_marks_->begin(gpu_sync_timing::TaaResolve);
    if(SUCCEEDED(hr)&&step(call<SetTextureFn>(SetTexture)(d,0,nullptr))&&step(call<SetRtFn>(SetRenderTarget)(d,0,color_surfaces_[next]))&&
        step(call<SetPsFn>(SetPixelShader)(d,program))&&step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,0,&constants.clip_to_previous[0][0],x3::temporal::kResolveRegisterCount))&&
        step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,x3::temporal::kLuminanceRegister,constants.luminance,1))&&
        step(call<SetTextureFn>(SetTexture)(d,0,in.color?in.color:scratch_))&&step(call<SetTextureFn>(SetTexture)(d,1,depths_[next]))&&
        step(call<SetTextureFn>(SetTexture)(d,2,history_.valid?colors_[current_]:nullptr))&&
        step(call<SetTextureFn>(SetTexture)(d,3,history_.valid?depths_[current_]:nullptr))&&
        step(call<SetTextureFn>(SetTexture)(d,4,in.motion_policy==MotionPolicy::PerPixel?in.motion:nullptr))&&
        step(call<SetTextureFn>(SetTexture)(d,5,supplemental?reactive_[next]:in.reactive))&&
        step(call<SetTextureFn>(SetTexture)(d,6,history_.valid&&mask?reactive_[current_]:nullptr))&&
        // S3, 5-tap programs only: the previous colour (s11) and, under a mask policy, the previous mask (s12) a second time
        // with LINEAR min / mag (clamp, single level, no sRGB; the block restores the samplers). s2 / s6 stay point.
        (!bilinear||(step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))&&step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR))&&
                     step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
                     step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_SRGBTEXTURE,FALSE))&&
                     step(call<SetSamplerFn>(SetSamplerState)(d,11,D3DSAMP_MAXMIPLEVEL,0))&&
                     step(call<SetTextureFn>(SetTexture)(d,11,history_.valid?colors_[current_]:nullptr))&&
                     // Without a mask policy s12 is unbound like s6 (normalize already cleared it; explicit so the
                     // program can never see a caller texture there).
                     (!mask?step(call<SetTextureFn>(SetTexture)(d,12,nullptr)):(step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))&&step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR))&&
                              step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
                              step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_SRGBTEXTURE,FALSE))&&
                              step(call<SetSamplerFn>(SetSamplerState)(d,12,D3DSAMP_MAXMIPLEVEL,0))&&
                              step(call<SetTextureFn>(SetTexture)(d,12,history_.valid?reactive_[current_]:nullptr))))))&&
        // Camera gate: the box targets at s9 / s10 (point, clamp, single level; the block restores the samplers).
        (!camera||(step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_MAGFILTER,D3DTEXF_POINT))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_SRGBTEXTURE,FALSE))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,9,D3DSAMP_MAXMIPLEVEL,0))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_MAGFILTER,D3DTEXF_POINT))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_SRGBTEXTURE,FALSE))&&
                   step(call<SetSamplerFn>(SetSamplerState)(d,10,D3DSAMP_MAXMIPLEVEL,0))&&
                   step(call<SetTextureFn>(SetTexture)(d,9,boxes_[0]))&&step(call<SetTextureFn>(SetTexture)(d,10,boxes_[1]))))&&
        // Flicker variants only: c24 (c24 and c25 for the age programs), and for the age weight the previous age at
        // s7 (point, clamp, single level; the block restores the sampler) and
        // the next age as RT1, which leaves the device again right after the
        // draw so no later quad of this run can write it.
        (!(flicker||far_on)||step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,x3::temporal::kFlickerRegister,flicker_constants,aged?2:1)))&&
        (!hold||step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,11,hold_constants,1)))&& // A': c11 (the block restores it)
        (!aged||(step(call<SetRsFn>(SetRenderState)(d,D3DRS_COLORWRITEENABLE1,15))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MAGFILTER,D3DTEXF_POINT))&&
                 step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&
                 step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))&&step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_SRGBTEXTURE,FALSE))&&
                 step(call<SetSamplerFn>(SetSamplerState)(d,7,D3DSAMP_MAXMIPLEVEL,0))&&
                 step(call<SetTextureFn>(SetTexture)(d,7,history_.valid?ages_[current_]:nullptr))&&
                 step(call<SetRtFn>(SetRenderTarget)(d,1,age_surfaces_[next])))))hr=quad(in.width,in.height);
    if(SUCCEEDED(hr)){diagnostics_.history_taps=bilinear?5u:16u;diagnostics_.region_hold=hold;}
    if(aged&&!lost(hr)){const HRESULT unbind=call<SetRtFn>(SetRenderTarget)(d,1,nullptr);if(SUCCEEDED(hr))hr=unbind;}
    if(SUCCEEDED(hr)&&in.reactive_policy==ReactivePolicy::RequiredMask){
        constants.options[2]=1; // mask snapshot; current s5 stays borrowed only for this run
        if(step(call<SetRtFn>(SetRenderTarget)(d,0,reactive_surfaces_[next]))&&
           step(call<SetPsFn>(SetPixelShader)(d,snapshot_))&&
           step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,7,constants.options,1)))hr=quad(in.width,in.height);
    }
    if(sync_marks_){sync_marks_->end(gpu_sync_timing::TaaResolve);if(SUCCEEDED(hr)&&(in.sharpen>0||draw_copy))sync_marks_->begin(gpu_sync_timing::TaaDisplay);}
    // Post-resolve sharpen (sharpen.h): with the history set complete, RCAS of
    // the new FP16 colour history is drawn into the caller's 8-bit surface
    // (the resolve's own input, already copied into the scratch), inside the
    // same state bracket and scene: no second capture/apply, no copy-back.
    // The history texture leaves RT0 before it is sampled. c23 is the only
    // constant register the sharpen touches; the block restores it.
    // Review 26: the sharpened draw is the display's, not the history's. A
    // lost device ends the run as anywhere else; any other failure of this
    // draw leaves the resolve in force (the history set is complete and is
    // published below) and hands the display to the caller's copy-back
    // (Output::sharpen_result names the failure; the caller counts them).
    bool display_written=false; HRESULT sharpen_result=S_FALSE;
    if(SUCCEEDED(hr)&&in.sharpen>0){
        x3::temporal::SharpenConstants sharpen{};
        auto sub=[&](HRESULT value){sharpen_result=value;return SUCCEEDED(value);};
        if(!x3::temporal::prepare_sharpen(sharpen,in.sharpen,in.width,in.height))sharpen_result=E_INVALIDARG;
        else if(sub(call<SetRtFn>(SetRenderTarget)(d,0,in.color_surface))&&sub(call<SetPsFn>(SetPixelShader)(d,sharpen_))&&
           sub(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,x3::temporal::kSharpenRegister,sharpen.values,1))&&
           sub(call<SetTextureFn>(SetTexture)(d,0,colors_[next])))sub(quad(in.width,in.height));
        display_written=SUCCEEDED(sharpen_result);
        if(lost(sharpen_result))hr=sharpen_result;
    }
    // Draw mode without a sharpened display: the identity draw of the new
    // history into the caller's 8-bit surface replaces the caller's
    // format-converting copy-back. Same failure policy as the sharpen draw:
    // the resolve stands, the display falls to the caller's copy-back.
    HRESULT copy_result=S_FALSE;
    if(SUCCEEDED(hr)&&draw_copy&&!display_written){
        auto sub=[&](HRESULT value){copy_result=value;return SUCCEEDED(value);};
        if(sub(call<SetRtFn>(SetRenderTarget)(d,0,in.color_surface))&&sub(call<SetPsFn>(SetPixelShader)(d,copy_))&&
           sub(call<SetTextureFn>(SetTexture)(d,0,colors_[next])))sub(quad(in.width,in.height));
        display_written=SUCCEEDED(copy_result);
        if(lost(copy_result))hr=copy_result;
    }
    if(sync_marks_)sync_marks_->end(gpu_sync_timing::TaaDisplay);
    if(own_scene&&!lost(hr)){const HRESULT end=call<SceneFn>(EndScene)(d);if(SUCCEEDED(hr)||lost(end))hr=end;}
    diagnostics_.ticks_draw+=stamp()-mark-depth_draw_ticks;
    diagnostics_.operation=hr;
    // Once loss is observed, ordinary state setters are not valid recovery. A
    // failed pass never publishes any member of a newly written history set.
    mark=stamp();
    diagnostics_.restoration=lost(hr)?hr:saved.restore();
    diagnostics_.ticks_apply=stamp()-mark;
    if(FAILED(hr)||FAILED(diagnostics_.restoration)){
        invalidate();return FAILED(diagnostics_.restoration)?diagnostics_.restoration:hr;
    }
    current_=next;reactive_policy_=in.reactive_policy;hold_history_=hold;
    if(reactive_policy_!=ReactivePolicy::Unavailable)history_.completed();
    diagnostics_.history_valid=history_.valid;++diagnostics_.completed_frames;++generation_;
    *out={colors_[current_],depths_[current_],generation_,used,reactive_[current_],color_surfaces_[current_],display_written,sharpen_result,copy_result,aged?ages_[current_]:nullptr,far_on?line_masks_[final_mask]:nullptr};return S_OK;
}
} // namespace x3m::renderer
