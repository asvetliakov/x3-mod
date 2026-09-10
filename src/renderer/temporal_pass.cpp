#include "temporal_pass.h"
#include <algorithm>
#include <cstring>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if(value) { value->Release(); value=nullptr; } }
bool lost(HRESULT hr) noexcept { return hr==D3DERR_DEVICELOST || hr==D3DERR_DEVICENOTRESET; }
struct SavedState {
    IDirect3DDevice9* device;
    IDirect3DStateBlock9* block=nullptr;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth=nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(IDirect3DDevice9* d,UINT n):device(d),count(n){}
    ~SavedState(){drop(block);for(auto& p:targets)drop(p);drop(depth);}
    HRESULT capture() noexcept {
        HRESULT hr=device->CreateStateBlock(D3DSBT_ALL,&block);
        if(FAILED(hr))return hr;
        for(UINT i=0;i<count;++i){hr=device->GetRenderTarget(i,&targets[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return hr;}
        hr=device->GetDepthStencilSurface(&depth);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return hr;
        hr=device->GetViewport(&viewport);if(FAILED(hr))return hr;
        return device->GetScissorRect(&scissor);
    }
    HRESULT restore() noexcept {
        HRESULT first=S_OK;
        auto attempt=[&](HRESULT hr){if(lost(hr)||(FAILED(hr)&&SUCCEEDED(first)))first=hr;return !lost(hr);};
        // Unbind pass inputs before returning application RTs. Restore RT0 before
        // secondary RTs/DS, and viewport last because SetRenderTarget resets it.
        for(UINT i=0;i<16;++i)if(!attempt(device->SetTexture(i,nullptr)))return first;
        for(UINT i=0;i<4;++i)if(!attempt(device->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr)))return first;
        if(!attempt(device->SetDepthStencilSurface(nullptr)))return first;
        for(UINT i=1;i<count;++i)if(!attempt(device->SetRenderTarget(i,nullptr)))return first;
        for(UINT i=0;i<count;++i)if(!attempt(device->SetRenderTarget(i,targets[i])))return first;
        if(!attempt(device->SetDepthStencilSurface(depth)))return first;
        if(!attempt(block->Apply()))return first;
        if(!attempt(device->SetViewport(&viewport)))return first;
        attempt(device->SetScissorRect(&scissor));
        return first;
    }
};
HRESULT texture_input(IDirect3DDevice9* device,IDirect3DTexture9* texture,UINT w,UINT h,D3DFORMAT format) noexcept {
    if(!texture)return E_INVALIDARG;
    D3DSURFACE_DESC desc{};
    HRESULT hr=texture->GetLevelDesc(0,&desc);if(FAILED(hr))return hr;
    if(desc.Width!=w||desc.Height!=h||desc.Format!=format||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return E_INVALIDARG;
    IDirect3DDevice9* owner=nullptr;hr=texture->GetDevice(&owner);
    if(FAILED(hr))return hr;
    const bool same=owner==device;drop(owner);
    return same?S_OK:E_INVALIDARG;
}
struct Vertex { float x,y,z,rhw,u,v; };
HRESULT quad(IDirect3DDevice9* d,UINT w,UINT h) noexcept {
    const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},
        {-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};
    return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
}
HRESULT normalize(IDirect3DDevice9* d,UINT rt_count,UINT streams,UINT w,UINT h) noexcept {
#define STEP(call) do { const HRESULT hresult=(call); if(FAILED(hresult))return hresult; } while(false)
    for(UINT i=0;i<16;++i)STEP(d->SetTexture(i,nullptr));
    for(UINT i=0;i<4;++i)STEP(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr));
    STEP(d->SetDepthStencilSurface(nullptr));
    for(UINT i=1;i<rt_count;++i)STEP(d->SetRenderTarget(i,nullptr));
    STEP(d->SetVertexShader(nullptr));
    STEP(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    STEP(d->SetIndices(nullptr));
    for(UINT i=0;i<streams;++i)STEP(d->SetStreamSourceFreq(i,1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,
                   D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,
                   D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,
                   D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,
                   D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})STEP(d->SetRenderState(state,FALSE));
    STEP(d->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE));
    STEP(d->SetRenderState(D3DRS_WRAP0,0));
    STEP(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
    STEP(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    STEP(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    STEP(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
    for(UINT i=0;i<5;++i){
        STEP(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));
        STEP(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        STEP(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        STEP(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        STEP(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        STEP(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));
        STEP(d->SetSamplerState(i,D3DSAMP_MAXMIPLEVEL,0));
    }
    D3DVIEWPORT9 viewport{0,0,w,h,0,1};STEP(d->SetViewport(&viewport));
#undef STEP
    return S_OK;
}
}
TemporalPass::~TemporalPass(){shutdown();}
void TemporalPass::invalidate() noexcept {history_.invalidate();diagnostics_.history_valid=false;++generation_;}
void TemporalPass::release_history() noexcept {
    invalidate();for(auto& p:color_surfaces_)drop(p);for(auto& p:depth_surfaces_)drop(p);
    for(auto& p:colors_)drop(p);
    for(auto& p:depths_)drop(p);
    width_=height_=current_=0;
}
void TemporalPass::shutdown() noexcept {release_history();drop(decoder_);drop(resolve_);device_=nullptr;render_targets_=streams_=0;}
void TemporalPass::before_reset() noexcept {shutdown();}
HRESULT TemporalPass::initialize(IDirect3DDevice9* d,const DWORD* decoder,const DWORD* resolve) noexcept {
    shutdown();diagnostics_={};if(!d||!decoder||!resolve)return E_INVALIDARG;
    D3DCAPS9 caps{};HRESULT hr=d->GetDeviceCaps(&caps);if(FAILED(hr))return hr;
    if(caps.PixelShaderVersion<D3DPS_VERSION(3,0)||!caps.NumSimultaneousRTs||caps.NumSimultaneousRTs>4||!caps.MaxStreams)return D3DERR_NOTAVAILABLE;
    device_=d;render_targets_=caps.NumSimultaneousRTs;streams_=caps.MaxStreams;
    hr=d->CreatePixelShader(decoder,&decoder_);
    if(SUCCEEDED(hr))hr=d->CreatePixelShader(resolve,&resolve_);
    if(FAILED(hr))shutdown();
    return hr;
}
HRESULT TemporalPass::allocate(UINT w,UINT h) noexcept {
    if(width_==w&&height_==h)return S_OK;
    release_history();HRESULT hr=S_OK;
    for(UINT i=0;i<2&&SUCCEEDED(hr);++i){
        hr=device_->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&colors_[i],nullptr);
        if(SUCCEEDED(hr))hr=colors_[i]->GetSurfaceLevel(0,&color_surfaces_[i]);
        if(SUCCEEDED(hr))hr=device_->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depths_[i],nullptr);
        if(SUCCEEDED(hr))hr=depths_[i]->GetSurfaceLevel(0,&depth_surfaces_[i]);
    }
    if(FAILED(hr)){release_history();return hr;}width_=w;height_=h;return S_OK;
}
HRESULT TemporalPass::run(const FrameInputs& in,Output* out) noexcept {
    if(out)*out={};
    diagnostics_.operation=diagnostics_.restoration=S_OK;
    auto fail=[&](HRESULT hr){invalidate();diagnostics_.operation=hr;return hr;};
    if(!out||!device_||!decoder_||!resolve_||!in.width||!in.height||in.caller_stateblock_recording||!in.caller_queries_idle||
        (in.motion_policy!=MotionPolicy::KnownCameraOnly&&in.motion_policy!=MotionPolicy::PerPixel))return fail(E_INVALIDARG);
    for(UINT i=0;i<2;++i)if((colors_[i]&&(in.color==colors_[i]||in.motion==colors_[i]))||
        (depths_[i]&&(in.depth_snapshot==depths_[i]||in.color==depths_[i]||in.motion==depths_[i])))return fail(E_INVALIDARG);
    HRESULT hr=texture_input(device_,in.color,in.width,in.height,D3DFMT_A16B16G16R16F);
    if(SUCCEEDED(hr))hr=texture_input(device_,in.depth_snapshot,in.width,in.height,D3DFMT_D24X8);
    if(SUCCEEDED(hr)&&in.motion_policy==MotionPolicy::PerPixel)hr=texture_input(device_,in.motion,in.width,in.height,D3DFMT_A32B32G32R32F);
    if(FAILED(hr))return fail(hr);
    hr=allocate(in.width,in.height);if(FAILED(hr))return fail(hr);
    history_.begin(in.width,in.height,in.epoch);
    if(in.camera_cut||!in.history_allowed)invalidate();
    x3::temporal::ResolveConstants constants{};std::copy(in.rejection,in.rejection+4,constants.rejection);
    if(!x3::temporal::prepare(constants,history_,in.clip_to_previous,in.current_jitter[0],in.current_jitter[1],
        in.previous_jitter[0],in.previous_jitter[1],in.weight,in.motion_policy==MotionPolicy::PerPixel))return fail(E_INVALIDARG);
    const bool used=history_.valid&&in.weight>0;
    SavedState saved(device_,render_targets_);hr=saved.capture();if(FAILED(hr))return fail(hr);
    const UINT next=current_^1;
    bool own_scene=false;
    hr=normalize(device_,render_targets_,streams_,in.width,in.height);
    auto step=[&](HRESULT value){hr=value;return SUCCEEDED(hr);};
    if(SUCCEEDED(hr)&&!in.caller_scene_open){hr=device_->BeginScene();own_scene=SUCCEEDED(hr);}
    if(SUCCEEDED(hr)&&step(device_->SetRenderTarget(0,depth_surfaces_[next]))&&
        step(device_->SetPixelShader(decoder_))&&step(device_->SetTexture(0,in.depth_snapshot)))hr=quad(device_,in.width,in.height);
    if(SUCCEEDED(hr)&&step(device_->SetTexture(0,nullptr))&&step(device_->SetRenderTarget(0,color_surfaces_[next]))&&
        step(device_->SetPixelShader(resolve_))&&step(device_->SetPixelShaderConstantF(0,&constants.clip_to_previous[0][0],8))&&
        step(device_->SetTexture(0,in.color))&&step(device_->SetTexture(1,depths_[next]))&&
        step(device_->SetTexture(2,history_.valid?colors_[current_]:nullptr))&&
        step(device_->SetTexture(3,history_.valid?depths_[current_]:nullptr))&&
        step(device_->SetTexture(4,in.motion_policy==MotionPolicy::PerPixel?in.motion:nullptr)))hr=quad(device_,in.width,in.height);
    if(own_scene&&!lost(hr)){const HRESULT end=device_->EndScene();if(SUCCEEDED(hr)||lost(end))hr=end;}
    diagnostics_.operation=hr;
    // Once loss is observed, ordinary state setters are not valid recovery. A
    // failed pass never publishes either half of a newly written history pair.
    diagnostics_.restoration=lost(hr)?hr:saved.restore();
    if(FAILED(hr)||FAILED(diagnostics_.restoration)){
        invalidate();return FAILED(diagnostics_.restoration)?diagnostics_.restoration:hr;
    }
    current_=next;history_.completed();diagnostics_.history_valid=true;++diagnostics_.completed_frames;++generation_;
    *out={colors_[current_],depths_[current_],generation_,used};return S_OK;
}
} // namespace x3m::renderer
