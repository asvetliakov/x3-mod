#include "rigid_motion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& p) noexcept { if(p){p->Release();p=nullptr;} }
bool lost(HRESULT h) noexcept {return h==D3DERR_DEVICELOST||h==D3DERR_DEVICENOTRESET;}
template<class T> HRESULT same_device(T* p,IDirect3DDevice9* d) noexcept {
    if(!p)return E_INVALIDARG;
    IDirect3DDevice9* owner=nullptr;HRESULT h=p->GetDevice(&owner);
    bool same=owner==d;drop(owner);return FAILED(h)?h:same?S_OK:E_INVALIDARG;
}
struct SavedState {
    IDirect3DDevice9* d;UINT count;
    IDirect3DStateBlock9* block=nullptr;
    IDirect3DSurface9* targets[4]{},*depth=nullptr;
    D3DVIEWPORT9 viewport{};RECT scissor{};
    SavedState(IDirect3DDevice9* device,UINT n):d(device),count(n){}
    ~SavedState(){drop(block);for(auto& p:targets)drop(p);drop(depth);}
    HRESULT capture() noexcept {
        HRESULT h=d->CreateStateBlock(D3DSBT_ALL,&block);if(FAILED(h))return h;
        for(UINT i=0;i<count;++i){h=d->GetRenderTarget(i,&targets[i]);if(FAILED(h)&&h!=D3DERR_NOTFOUND)return h;}
        h=d->GetDepthStencilSurface(&depth);if(FAILED(h)&&h!=D3DERR_NOTFOUND)return h;
        h=d->GetViewport(&viewport);if(FAILED(h))return h;
        return d->GetScissorRect(&scissor);
    }
    HRESULT restore() noexcept {
        HRESULT first=S_OK;
        auto attempt=[&](HRESULT h){if(lost(h)||(FAILED(h)&&SUCCEEDED(first)))first=h;return !lost(h);};
        for(UINT i=0;i<16;++i)if(!attempt(d->SetTexture(i,nullptr)))return first;
        for(UINT i=0;i<4;++i)if(!attempt(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr)))return first;
        if(!attempt(d->SetDepthStencilSurface(nullptr)))return first;
        for(UINT i=1;i<count;++i)if(!attempt(d->SetRenderTarget(i,nullptr)))return first;
        for(UINT i=0;i<count;++i)if(!attempt(d->SetRenderTarget(i,targets[i])))return first;
        if(!attempt(d->SetDepthStencilSurface(depth)))return first;
        if(!attempt(block->Apply()))return first;
        if(!attempt(d->SetViewport(&viewport)))return first;
        attempt(d->SetScissorRect(&scissor));return first;
    }
};
bool matrix_valid(const float* m) noexcept {
    for(UINT i=0;i<16;++i)if(!std::isfinite(m[i])||std::fabs(m[i])>1e15f)return false;
    return true;
}
HRESULT validate_draw(IDirect3DDevice9* d,const RigidMotionDraw& x) noexcept {
    const UINT position_bytes=x.position_type==D3DDECLTYPE_FLOAT3?12:x.position_type==D3DDECLTYPE_FLOAT16_4?8:0;
    if(!position_bytes||x.semantic!=RigidPositionSemantic::PositionXyzWOneRowDots||!x.correspondence_attested||
       x.stream_frequency!=1||!x.vertices||x.stride<position_bytes||x.position_offset>x.stride-position_bytes||
       !x.primitive_count||(x.topology!=D3DPT_TRIANGLELIST&&x.topology!=D3DPT_TRIANGLESTRIP)||
       (x.cull!=D3DCULL_NONE&&x.cull!=D3DCULL_CW&&x.cull!=D3DCULL_CCW)||
       !matrix_valid(x.current_wvp)||!matrix_valid(x.previous_wvp))return E_INVALIDARG;
    HRESULT h=same_device(x.vertices,d);if(FAILED(h))return h;
    D3DVERTEXBUFFER_DESC vb{};h=x.vertices->GetDesc(&vb);if(FAILED(h))return h;
    const std::uint64_t count=x.topology==D3DPT_TRIANGLELIST?3ull*x.primitive_count:2ull+x.primitive_count;
    std::int64_t first=x.start_vertex;std::uint64_t vertices=count;
    if(x.indices){
        h=same_device(x.indices,d);if(FAILED(h))return h;
        D3DINDEXBUFFER_DESC ib{};h=x.indices->GetDesc(&ib);if(FAILED(h))return h;
        if((ib.Format!=D3DFMT_INDEX16&&ib.Format!=D3DFMT_INDEX32)||!x.vertex_count)return E_INVALIDARG;
        const UINT size=ib.Format==D3DFMT_INDEX16?2:4;
        if((std::uint64_t(x.start_index)+count)*size>ib.Size)return E_INVALIDARG;
        first=std::int64_t(x.base_vertex)+x.minimum_vertex;vertices=x.vertex_count;
    }
    // Index contents are never read back. Exact trusted original min/count/base
    // must describe every referenced vertex; unchanged-content proof is external.
    const std::uint64_t base=std::uint64_t(x.stream_offset)+x.position_offset;
    if(first<0||base>vb.Size||position_bytes>vb.Size-base)return E_INVALIDARG;
    // Divide the known finite allocation instead of multiplying hostile counts
    // by stride: even a uint64 product can wrap for UINT_MAX draw arguments.
    if(std::uint64_t(first)+vertices-1>(vb.Size-base-position_bytes)/x.stride)return E_INVALIDARG;
    return S_OK;
}
HRESULT normalize(IDirect3DDevice9* d,UINT targets,UINT streams,UINT w,UINT h,
                  IDirect3DSurface9* motion) noexcept {
#define STEP(call) do{HRESULT result=(call);if(FAILED(result))return result;}while(false)
    for(UINT i=0;i<16;++i)STEP(d->SetTexture(i,nullptr));
    for(UINT i=0;i<4;++i)STEP(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr));
    STEP(d->SetDepthStencilSurface(nullptr));
    for(UINT i=1;i<targets;++i)STEP(d->SetRenderTarget(i,nullptr));
    STEP(d->SetRenderTarget(0,motion));
    for(UINT i=0;i<streams;++i)STEP(d->SetStreamSourceFreq(i,1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,
                   D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,
                   D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,
                   D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,
                   D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})STEP(d->SetRenderState(state,FALSE));
    STEP(d->SetRenderState(D3DRS_CLIPPING,TRUE));
    STEP(d->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE));
    STEP(d->SetRenderState(D3DRS_WRAP0,0));
    STEP(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
    STEP(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    STEP(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    STEP(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
    STEP(d->SetRenderState(D3DRS_DEPTHBIAS,0));
    STEP(d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0));
    D3DVIEWPORT9 viewport{0,0,w,h,0,1};STEP(d->SetViewport(&viewport));
    STEP(d->SetIndices(nullptr));
#undef STEP
    return S_OK;
}
HRESULT initialize_invalid(IDirect3DDevice9* d) noexcept {
    // Stay on the programmable path for initialization as well as geometry.
    // The oversized clip-space triangle covers the full current viewport.
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    HRESULT h=d->SetVertexShaderConstantF(0,identity,4);if(FAILED(h))return h;
    const float v[]={-1,1,0,3,1,0,-1,-3,0};
    return d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,3*sizeof(float));
}
} // namespace
RigidMotionPass::~RigidMotionPass(){shutdown();}
void RigidMotionPass::shutdown() noexcept {
    for(auto& p:declarations_)drop(p);
    drop(vertex_);drop(pixel_);device_=nullptr;targets_=streams_=0;++generation_;
}
void RigidMotionPass::before_reset() noexcept {shutdown();}
HRESULT RigidMotionPass::initialize(IDirect3DDevice9* d,const DWORD* vs,const DWORD* ps) noexcept {
    shutdown();diagnostics_={};if(!d||!vs||!ps)return E_INVALIDARG;
    D3DCAPS9 caps{};HRESULT h=d->GetDeviceCaps(&caps);if(FAILED(h))return h;
    if(caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0)||
       !caps.NumSimultaneousRTs||caps.NumSimultaneousRTs>4||!caps.MaxStreams)return D3DERR_NOTAVAILABLE;
    device_=d;targets_=caps.NumSimultaneousRTs;streams_=caps.MaxStreams;
    h=d->CreateVertexShader(vs,&vertex_);if(SUCCEEDED(h))h=d->CreatePixelShader(ps,&pixel_);
    for(UINT i=0;i<2&&SUCCEEDED(h);++i){
        const D3DVERTEXELEMENT9 elements[]={{0,0,BYTE(i?D3DDECLTYPE_FLOAT16_4:D3DDECLTYPE_FLOAT3),D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        h=d->CreateVertexDeclaration(elements,&declarations_[i]);
    }
    if(FAILED(h))shutdown();
    return h;
}
HRESULT RigidMotionPass::run(const RigidMotionInputs& in,RigidMotionOutput* out) noexcept {
    if(out)*out={};
    diagnostics_={};++generation_;
    auto fail=[&](HRESULT h){diagnostics_.operation=h;return h;};
    if(!out||!device_||!in.width||!in.height||!in.scene_depth_current||
       in.caller_stateblock_recording||!in.caller_queries_idle||in.draw_count>65536||
       (in.draw_count&&!in.draws)||!std::isfinite(in.previous_jitter[0])||
       !std::isfinite(in.previous_jitter[1]))return fail(E_INVALIDARG);
    HRESULT h=same_device(in.motion,device_);if(SUCCEEDED(h))h=same_device(in.scene_depth,device_);
    if(FAILED(h))return fail(h);
    D3DSURFACE_DESC color{},depth{};
    h=in.motion->GetLevelDesc(0,&color);if(SUCCEEDED(h))h=in.scene_depth->GetDesc(&depth);
    if(FAILED(h))return fail(h);
    if(color.Width!=in.width||color.Height!=in.height||color.Format!=D3DFMT_A32B32G32R32F||
       !(color.Usage&D3DUSAGE_RENDERTARGET)||color.MultiSampleType!=D3DMULTISAMPLE_NONE||
       depth.Width!=in.width||depth.Height!=in.height||depth.Format!=D3DFMT_D24X8||
       !(depth.Usage&D3DUSAGE_DEPTHSTENCIL)||depth.MultiSampleType!=D3DMULTISAMPLE_NONE)return fail(E_INVALIDARG);
    for(std::size_t i=0;i<in.draw_count;++i){h=validate_draw(device_,in.draws[i]);if(FAILED(h))return fail(h);}
    IDirect3DSurface9* target=nullptr;h=in.motion->GetSurfaceLevel(0,&target);if(FAILED(h))return fail(h);
    SavedState saved(device_,targets_);h=saved.capture();if(FAILED(h)){drop(target);return fail(h);}
    h=normalize(device_,targets_,streams_,in.width,in.height,target);drop(target);
    bool own_scene=false;auto step=[&](HRESULT value){h=value;return SUCCEEDED(h);};
    const float constants[8]={1.f/in.width,1.f/in.height,in.previous_jitter[0]/in.width,
        in.previous_jitter[1]/in.height,0,0,0,0};
    if(SUCCEEDED(h)&&!in.caller_scene_open){h=device_->BeginScene();own_scene=SUCCEEDED(h);}
    if(SUCCEEDED(h)&&step(device_->SetVertexDeclaration(declarations_[0]))&&
       step(device_->SetVertexShader(vertex_))&&step(device_->SetPixelShader(pixel_))&&
       step(device_->SetPixelShaderConstantF(0,constants,2)))h=initialize_invalid(device_);
    const float mode[4]={1,0,0,0};
    if(SUCCEEDED(h)&&step(device_->SetDepthStencilSurface(in.scene_depth))&&
       step(device_->SetRenderState(D3DRS_ZENABLE,TRUE))&&step(device_->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL))&&
       step(device_->SetVertexShader(vertex_)))
        h=device_->SetPixelShaderConstantF(1,mode,1);
    for(std::size_t i=0;i<in.draw_count&&SUCCEEDED(h);++i){
        const auto& x=in.draws[i];
        if(step(device_->SetVertexDeclaration(declarations_[x.position_type==D3DDECLTYPE_FLOAT16_4?1:0]))&&
           step(device_->SetStreamSource(0,x.vertices,x.stream_offset+x.position_offset,x.stride))&&
           step(device_->SetIndices(x.indices))&&step(device_->SetRenderState(D3DRS_CULLMODE,x.cull))&&
           step(device_->SetVertexShaderConstantF(0,x.current_wvp,4))&&
           step(device_->SetVertexShaderConstantF(4,x.previous_wvp,4))){
            h=x.indices?device_->DrawIndexedPrimitive(x.topology,x.base_vertex,x.minimum_vertex,x.vertex_count,x.start_index,x.primitive_count):
                        device_->DrawPrimitive(x.topology,x.start_vertex,x.primitive_count);
            if(SUCCEEDED(h))++diagnostics_.completed_draws;
        }
    }
    if(own_scene&&!lost(h)){HRESULT end=device_->EndScene();if(SUCCEEDED(h)||lost(end))h=end;}
    diagnostics_.operation=h;diagnostics_.restoration=lost(h)?h:saved.restore();
    if(FAILED(diagnostics_.restoration))return diagnostics_.restoration;
    if(FAILED(h))return h;
    *out={in.motion,generation_};return S_OK;
}
} // namespace x3m::renderer
