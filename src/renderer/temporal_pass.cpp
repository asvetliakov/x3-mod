#include "temporal_pass.h"
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
    GetDeviceCaps = 7, CreateTexture = 23, StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83, SetFVF = 89,
    SetVertexShader = 92, SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106,
    SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using CapsFn = HRESULT(WINAPI*)(D, D3DCAPS9*);
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
HRESULT texture_input(IDirect3DDevice9* device,IDirect3DTexture9* texture,UINT w,UINT h,D3DFORMAT format) noexcept {
    if(!texture)return E_INVALIDARG;
    D3DSURFACE_DESC desc{};
    HRESULT hr=texture->GetLevelDesc(0,&desc);if(FAILED(hr))return hr;
    if(desc.Width!=w||desc.Height!=h||desc.Format!=format||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return E_INVALIDARG;
    return same_device(device,texture);
}
// The 8-bit main target: a default-pool, non-multisampled 32-bit surface that
// StretchRect can read. It need not be a texture level.
HRESULT surface_input(IDirect3DDevice9* device,IDirect3DSurface9* surface,UINT w,UINT h) noexcept {
    if(!surface)return E_INVALIDARG;
    D3DSURFACE_DESC desc{};
    HRESULT hr=surface->GetDesc(&desc);if(FAILED(hr))return hr;
    if(desc.Width!=w||desc.Height!=h||(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8)||
       desc.Pool!=D3DPOOL_DEFAULT||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return E_INVALIDARG;
    return same_device(device,surface);
}
struct Vertex { float x,y,z,rhw,u,v; };
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
HRESULT TemporalPass::quad(UINT w,UINT h) noexcept {
    const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},
        {-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};
    return call<DrawUpFn>(DrawPrimitiveUP)(device_,D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
}
HRESULT TemporalPass::normalize(UINT w,UINT h) noexcept {
    D d=device_;
#define STEP(call) do { const HRESULT hresult=(call); if(FAILED(hresult))return hresult; } while(false)
    for(UINT i=0;i<16;++i)STEP(call<SetTextureFn>(SetTexture)(d,i,nullptr));
    for(UINT i=0;i<4;++i)STEP(call<SetTextureFn>(SetTexture)(d,D3DVERTEXTEXTURESAMPLER0+i,nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d,nullptr));
    for(UINT i=1;i<render_targets_;++i)STEP(call<SetRtFn>(SetRenderTarget)(d,i,nullptr));
    STEP(call<SetVsFn>(SetVertexShader)(d,nullptr));
    STEP(call<SetFvfFn>(SetFVF)(d,D3DFVF_XYZRHW|D3DFVF_TEX1));
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
    // The quad is pre-transformed with no vertex shader, so stage 0's
    // fixed-function coordinate index and texture transform still shape the
    // TEXCOORD0 the resolve reads (verified on the Preview backend by the
    // fixture's hostile state); both go back through the state block.
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
    drop(scratch_surface_);
    for(auto& p:colors_)drop(p);
    for(auto& p:depths_)drop(p);
    for(auto& p:reactive_)drop(p);
    drop(scratch_);
    reactive_policy_=ReactivePolicy::Unavailable;
    width_=height_=current_=0;
}
void TemporalPass::shutdown() noexcept {release_history();drop(block_);drop(decoder_);drop(resolve_);device_=nullptr;vtable_=nullptr;render_targets_=streams_=0;diagnostics_.reset_pending=false;}
// Every owned texture and the state block must not exist across Reset; the
// compiled shaders survive it. Runs are refused until after_reset succeeds.
void TemporalPass::before_reset() noexcept {release_history();drop(block_);diagnostics_.reset_pending=device_!=nullptr;}
void TemporalPass::after_reset(HRESULT result) noexcept {invalidate();if(SUCCEEDED(result))diagnostics_.reset_pending=false;}
HRESULT TemporalPass::initialize(IDirect3DDevice9* d,const DWORD* decoder,const DWORD* resolve,void* const* native_vtable) noexcept {
    shutdown();diagnostics_={};if(!d||!resolve)return E_INVALIDARG;
    device_=d;vtable_=native_vtable;
    D3DCAPS9 caps{};HRESULT hr=call<CapsFn>(GetDeviceCaps)(d,&caps);
    if(SUCCEEDED(hr)&&(caps.PixelShaderVersion<D3DPS_VERSION(3,0)||!caps.NumSimultaneousRTs||caps.NumSimultaneousRTs>4||!caps.MaxStreams))hr=D3DERR_NOTAVAILABLE;
    if(FAILED(hr)){device_=nullptr;vtable_=nullptr;return hr;}
    render_targets_=caps.NumSimultaneousRTs;streams_=caps.MaxStreams;
    if(decoder)hr=call<CreatePsFn>(CreatePixelShader)(d,decoder,&decoder_);
    if(SUCCEEDED(hr))hr=call<CreatePsFn>(CreatePixelShader)(d,resolve,&resolve_);
    if(FAILED(hr))shutdown();
    return hr;
}
HRESULT TemporalPass::allocate(UINT w,UINT h,bool reactive) noexcept {
    if(width_==w&&height_==h&&bool(reactive_[0])==reactive)return S_OK;
    release_history();HRESULT hr=S_OK;
    for(UINT i=0;i<2&&SUCCEEDED(hr);++i){
        hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&colors_[i],nullptr);
        if(SUCCEEDED(hr))hr=colors_[i]->GetSurfaceLevel(0,&color_surfaces_[i]);
        if(SUCCEEDED(hr))hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depths_[i],nullptr);
        if(SUCCEEDED(hr))hr=depths_[i]->GetSurfaceLevel(0,&depth_surfaces_[i]);
        if(SUCCEEDED(hr)&&reactive)hr=call<CreateTextureFn>(CreateTexture)(device_,w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&reactive_[i],nullptr);
        if(SUCCEEDED(hr)&&reactive)hr=reactive_[i]->GetSurfaceLevel(0,&reactive_surfaces_[i]);
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
HRESULT TemporalPass::ensure_block() noexcept {
    if(block_)return S_OK;
    const HRESULT hr=call<CreateBlockFn>(CreateStateBlock)(device_,D3DSBT_ALL,&block_);
    if(FAILED(hr))drop(block_);
    return hr;
}
HRESULT TemporalPass::run(const FrameInputs& in,Output* out) noexcept {
    if(out)*out={};
    diagnostics_.operation=diagnostics_.restoration=S_OK;
    auto fail=[&](HRESULT hr){invalidate();diagnostics_.operation=hr;return hr;};
    const bool sentinel=in.reactive_policy==ReactivePolicy::DerivedFromDepthSentinel;
    if(!out||!device_||!resolve_||diagnostics_.reset_pending||!in.width||!in.height||in.caller_stateblock_recording||!in.caller_queries_idle||
        (in.motion_policy!=MotionPolicy::KnownCameraOnly&&in.motion_policy!=MotionPolicy::PerPixel)||
        (in.reactive_policy!=ReactivePolicy::Unavailable&&in.reactive_policy!=ReactivePolicy::KnownNonReactive&&
         in.reactive_policy!=ReactivePolicy::RequiredMask&&!sentinel)||
        (in.reactive_policy!=ReactivePolicy::RequiredMask&&in.reactive)||
        bool(in.color)==bool(in.color_surface)||bool(in.depth_snapshot)==bool(in.current_depth)||
        (in.depth_snapshot&&!decoder_)||(sentinel&&!in.current_depth))return fail(E_INVALIDARG);
    for(UINT i=0;i<2;++i){
        for(auto* owned:{colors_[i],depths_[i],reactive_[i],scratch_})
            if(owned&&(in.color==owned||in.depth_snapshot==owned||in.current_depth==owned||in.motion==owned||in.reactive==owned))return fail(E_INVALIDARG);
        if(in.color_surface&&(in.color_surface==color_surfaces_[i]||in.color_surface==scratch_surface_))return fail(E_INVALIDARG);
    }
    HRESULT hr=in.color?texture_input(device_,in.color,in.width,in.height,D3DFMT_A16B16G16R16F):surface_input(device_,in.color_surface,in.width,in.height);
    if(SUCCEEDED(hr))hr=in.current_depth?texture_input(device_,in.current_depth,in.width,in.height,D3DFMT_R32F):texture_input(device_,in.depth_snapshot,in.width,in.height,D3DFMT_D24X8);
    if(SUCCEEDED(hr)&&in.motion_policy==MotionPolicy::PerPixel)hr=texture_input(device_,in.motion,in.width,in.height,D3DFMT_A32B32G32R32F);
    if(SUCCEEDED(hr)&&in.reactive_policy==ReactivePolicy::RequiredMask)hr=texture_input(device_,in.reactive,in.width,in.height,D3DFMT_R32F);
    if(FAILED(hr))return fail(hr);
    hr=allocate(in.width,in.height,in.reactive_policy==ReactivePolicy::RequiredMask);if(FAILED(hr))return fail(hr);
    hr=ensure_block();if(FAILED(hr))return fail(hr);
    history_.begin(in.width,in.height,in.epoch);
    if(in.camera_cut||in.cut||!in.history_allowed||in.reactive_policy==ReactivePolicy::Unavailable||
       in.reactive_policy!=reactive_policy_)invalidate();
    x3::temporal::ResolveConstants constants{};std::copy(in.rejection,in.rejection+4,constants.rejection);
    if(!x3::temporal::prepare(constants,history_,in.clip_to_previous,in.current_jitter[0],in.current_jitter[1],
        in.previous_jitter[0],in.previous_jitter[1],in.weight,in.motion_policy==MotionPolicy::PerPixel,
        in.reactive_policy==ReactivePolicy::RequiredMask,sentinel))return fail(E_INVALIDARG);
    const bool used=history_.valid&&in.weight>0;
    SavedState saved(*this,block_,render_targets_);hr=saved.capture();if(FAILED(hr))return fail(hr);
    const UINT next=current_^1;
    bool own_scene=false;
    hr=normalize(in.width,in.height);
    auto step=[&](HRESULT value){hr=value;return SUCCEEDED(hr);};
    D d=device_;
    // Copies touch no device state; they run after normalize so the scratch and
    // the next depth history are bound nowhere. StretchRect is legal inside or
    // outside a scene. No sRGB flag is set on any sampler or target, so the
    // 8-bit copy is a plain UNORM-to-FP16 conversion of the linear-encoded data.
    if(SUCCEEDED(hr)&&in.color_surface&&step(ensure_scratch()))hr=call<StretchFn>(StretchRect)(d,in.color_surface,nullptr,scratch_surface_,nullptr,D3DTEXF_POINT);
    if(SUCCEEDED(hr)&&in.current_depth){
        IDirect3DSurface9* source=nullptr;
        if(step(in.current_depth->GetSurfaceLevel(0,&source)))hr=call<StretchFn>(StretchRect)(d,source,nullptr,depth_surfaces_[next],nullptr,D3DTEXF_POINT);
        drop(source);
    }
    if(SUCCEEDED(hr)&&!in.caller_scene_open){hr=call<SceneFn>(BeginScene)(d);own_scene=SUCCEEDED(hr);}
    if(SUCCEEDED(hr)&&in.depth_snapshot&&step(call<SetRtFn>(SetRenderTarget)(d,0,depth_surfaces_[next]))&&
        step(call<SetPsFn>(SetPixelShader)(d,decoder_))&&step(call<SetTextureFn>(SetTexture)(d,0,in.depth_snapshot)))hr=quad(in.width,in.height);
    if(SUCCEEDED(hr)&&step(call<SetTextureFn>(SetTexture)(d,0,nullptr))&&step(call<SetRtFn>(SetRenderTarget)(d,0,color_surfaces_[next]))&&
        step(call<SetPsFn>(SetPixelShader)(d,resolve_))&&step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,0,&constants.clip_to_previous[0][0],8))&&
        step(call<SetTextureFn>(SetTexture)(d,0,in.color?in.color:scratch_))&&step(call<SetTextureFn>(SetTexture)(d,1,depths_[next]))&&
        step(call<SetTextureFn>(SetTexture)(d,2,history_.valid?colors_[current_]:nullptr))&&
        step(call<SetTextureFn>(SetTexture)(d,3,history_.valid?depths_[current_]:nullptr))&&
        step(call<SetTextureFn>(SetTexture)(d,4,in.motion_policy==MotionPolicy::PerPixel?in.motion:nullptr))&&
        step(call<SetTextureFn>(SetTexture)(d,5,in.reactive))&&
        step(call<SetTextureFn>(SetTexture)(d,6,history_.valid&&in.reactive_policy==ReactivePolicy::RequiredMask?reactive_[current_]:nullptr)))hr=quad(in.width,in.height);
    if(SUCCEEDED(hr)&&in.reactive_policy==ReactivePolicy::RequiredMask){
        constants.options[2]=1; // mask snapshot; current s5 stays borrowed only for this run
        if(step(call<SetRtFn>(SetRenderTarget)(d,0,reactive_surfaces_[next]))&&
           step(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,7,constants.options,1)))hr=quad(in.width,in.height);
    }
    if(own_scene&&!lost(hr)){const HRESULT end=call<SceneFn>(EndScene)(d);if(SUCCEEDED(hr)||lost(end))hr=end;}
    diagnostics_.operation=hr;
    // Once loss is observed, ordinary state setters are not valid recovery. A
    // failed pass never publishes any member of a newly written history set.
    diagnostics_.restoration=lost(hr)?hr:saved.restore();
    if(FAILED(hr)||FAILED(diagnostics_.restoration)){
        invalidate();return FAILED(diagnostics_.restoration)?diagnostics_.restoration:hr;
    }
    current_=next;reactive_policy_=in.reactive_policy;
    if(reactive_policy_!=ReactivePolicy::Unavailable)history_.completed();
    diagnostics_.history_valid=history_.valid;++diagnostics_.completed_frames;++generation_;
    *out={colors_[current_],depths_[current_],generation_,used,reactive_[current_],color_surfaces_[current_]};return S_OK;
}
} // namespace x3m::renderer
