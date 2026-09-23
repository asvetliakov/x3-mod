// Detached stored-density FogPass fixture (checkpoint 3): the production pass with its cache
// manager, worker, UpdateSurface slab uploads, readiness ramps, Reset and device-loss recovery,
// hostile caller state, the capability refusal beside the legacy path, and CPU references for
// the repair draw, odd target sizes, shafts and the storage seam / lane 3->0 wrap.
// Timings are render-thread CPU under this harness, never game FPS or GPU cost.
#include "../../src/fog/fog_density_cache.h"
#include "fog_density_cpu_march.h"
#include "fog_dust_motes_cpu.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/fog_pass.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace x3m::fog;
using namespace x3m::renderer;
using Device=IDirect3DDevice9*;
template<class T> struct Com{T* p=nullptr;~Com(){reset();}void reset(){if(p)p->Release();p=nullptr;}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;};
void check(HRESULT hr,const char* name){if(FAILED(hr)){std::printf("FAIL api=%s hr=%08lx\n",name,(unsigned long)hr);throw std::runtime_error(name);}}
unsigned checks=0,failures=0,state_restorations=0;
bool require(bool value,const char* name){++checks;failures+=!value;std::printf("CHECK %s %s\n",name,value?"PASS":"FAIL");std::fflush(stdout);return value;}
constexpr double kTan30=0.5773502691896257;
LONGLONG ticks(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;}
double seconds(LONGLONG from){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return double(ticks()-from)/double(f.QuadPart);}
double percentile(std::vector<double> v,double p){if(v.empty())return 0;std::sort(v.begin(),v.end());const double at=p*(v.size()-1);const std::size_t i=std::size_t(at);return i+1<v.size()?v[i]+(at-i)*(v[i+1]-v[i]):v[i];}

// Native table handed to the pass, with UpdateSurface routed through a counter / loss injector.
void* table[119];
using UpdateSurfaceFn=HRESULT(WINAPI*)(Device,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*);
UpdateSurfaceFn real_update=nullptr;unsigned update_calls=0,lose_updates=0;
using CreateTextureFn=HRESULT(WINAPI*)(Device,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
CreateTextureFn real_create_texture=nullptr;bool refuse_rgba8_targets=false; // the visibility grid's target: injected creation failure
UINT refuse_fp16_target_width=0; // the quarter march target (step C): an FP16 render target of this width fails when set
HRESULT WINAPI create_texture_stub(Device d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* handle){
    if(refuse_rgba8_targets&&format==D3DFMT_A8R8G8B8&&(usage&D3DUSAGE_RENDERTARGET)){SetLastError(0xbad23);return D3DERR_INVALIDCALL;}
    if(refuse_fp16_target_width&&w==refuse_fp16_target_width&&format==D3DFMT_A16B16G16R16F&&(usage&D3DUSAGE_RENDERTARGET)){SetLastError(0xbad25);return D3DERR_OUTOFVIDEOMEMORY;}
    return real_create_texture(d,w,h,levels,usage,format,pool,out,handle);
}
// CreatePixelShader counted, and refused for the 24-far-bin march when injected (fog-gpu-cost.md step B: a pair that
// cannot be built must leave the working pair drawing).
constexpr DWORD far24_march_words[]={
#include "../../src/renderer/fog_density_march_look_far24_program_inc.h"
};
// The quarter-resolution march programs (step C), refused when injected: both far-bin counts.
constexpr DWORD q4_march_words[]={
#include "../../src/renderer/fog_density_march_look_q4_program_inc.h"
};
constexpr DWORD far24_q4_march_words[]={
#include "../../src/renderer/fog_density_march_look_far24_q4_program_inc.h"
};
// The default (scale 2, 40 far bins) march, refused when injected: the double failure after a Reset (step C).
constexpr DWORD default_march_words[]={
#include "../../src/renderer/fog_density_march_look_program_inc.h"
};
using CreatePixelShaderFn=HRESULT(WINAPI*)(Device,const DWORD*,IDirect3DPixelShader9**);
CreatePixelShaderFn real_create_pixel_shader=nullptr;bool refuse_far24_programs=false,refuse_q4_programs=false,refuse_default_march=false;unsigned pixel_shader_creates=0;
HRESULT WINAPI create_pixel_shader_stub(Device d,const DWORD* words,IDirect3DPixelShader9** out){
    ++pixel_shader_creates;
    if(refuse_far24_programs&&words&&!std::memcmp(words,far24_march_words,sizeof far24_march_words)){SetLastError(0xbad24);return E_OUTOFMEMORY;}
    if(refuse_default_march&&words&&!std::memcmp(words,default_march_words,sizeof default_march_words)){SetLastError(0xbad27);return E_OUTOFMEMORY;}
    if(refuse_q4_programs&&words&&(!std::memcmp(words,q4_march_words,sizeof q4_march_words)||!std::memcmp(words,far24_q4_march_words,sizeof far24_q4_march_words))){SetLastError(0xbad26);return E_OUTOFMEMORY;}
    return real_create_pixel_shader(d,words,out);
}
HRESULT WINAPI update_stub(Device d,IDirect3DSurface9* s,const RECT* r,IDirect3DSurface9* t,const POINT* p){
    ++update_calls;if(lose_updates){--lose_updates;SetLastError(0xbad19);return D3DERR_DEVICELOST;}return real_update(d,s,r,t,p);
}

struct Case{double cam[3],r[3],u[3],f[3];};
void upload(Device d,UINT w,UINT h,D3DFORMAT format,UINT stride,const void* bytes,DWORD usage,IDirect3DTexture9** out){
    Com<IDirect3DTexture9> sys;check(d->CreateTexture(w,h,1,0,format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr),"upload texture");
    D3DLOCKED_RECT lock{};check(sys->LockRect(0,&lock,nullptr,0),"upload lock");
    for(UINT y=0;y<h;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,static_cast<const char*>(bytes)+std::size_t(y)*w*stride,std::size_t(w)*stride);
    check(sys->UnlockRect(0),"upload unlock");check(d->CreateTexture(w,h,1,usage,format,D3DPOOL_DEFAULT,out,nullptr),"input texture");check(d->UpdateTexture(sys.p,*out),"update input");
}
std::vector<std::uint8_t> surface_bytes(Device d,IDirect3DSurface9* rt){
    D3DSURFACE_DESC desc{};check(rt->GetDesc(&desc),"readback desc");const UINT stride=desc.Format==D3DFMT_A32B32G32R32F?16:8;
    Com<IDirect3DSurface9> sys;check(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr),"readback surface");
    check(d->GetRenderTargetData(rt,sys.p),"readback");D3DLOCKED_RECT lock{};check(sys->LockRect(&lock,nullptr,D3DLOCK_READONLY),"readback lock");
    std::vector<std::uint8_t> out(std::size_t(desc.Width)*desc.Height*stride);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(out.data()+std::size_t(y)*desc.Width*stride,static_cast<const std::uint8_t*>(lock.pBits)+std::size_t(y)*lock.Pitch,std::size_t(desc.Width)*stride);
    check(sys->UnlockRect(),"readback unlock");return out;
}
std::vector<float> half_image(const std::vector<std::uint8_t>& bytes){
    std::vector<float> out(bytes.size()/2);for(std::size_t i=0;i<out.size();++i){std::uint16_t word;std::memcpy(&word,bytes.data()+2*i,2);out[i]=half_to_float(word);}return out;
}

// Device state the caller owns, compared byte for byte around every pass entry point.
struct Snapshot{
    std::vector<unsigned char> bytes;
    template<class T>void add(const T& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);bytes.insert(bytes.end(),p,p+sizeof v);}
    template<class T>void object(T* v){add(reinterpret_cast<std::uintptr_t>(v));if(v)v->Release();}
    Snapshot(Device d,const D3DCAPS9& caps){
        for(UINT i=0;i<caps.NumSimultaneousRTs;++i){IDirect3DSurface9* s=nullptr;add(d->GetRenderTarget(i,&s));object(s);}
        IDirect3DSurface9* ds=nullptr;add(d->GetDepthStencilSurface(&ds));object(ds);
        D3DVIEWPORT9 vp{};check(d->GetViewport(&vp),"state viewport");add(vp);RECT rect{};check(d->GetScissorRect(&rect),"state scissor");add(rect);
        IDirect3DVertexShader9* vs=nullptr;check(d->GetVertexShader(&vs),"state vs");object(vs);IDirect3DPixelShader9* ps=nullptr;check(d->GetPixelShader(&ps),"state ps");object(ps);
        IDirect3DVertexDeclaration9* decl=nullptr;check(d->GetVertexDeclaration(&decl),"state declaration");object(decl);DWORD fvf=0;check(d->GetFVF(&fvf),"state fvf");add(fvf);
        for(UINT i=0;i<caps.MaxStreams;++i){IDirect3DVertexBuffer9* vb=nullptr;UINT offset=0,stride=0,freq=0;check(d->GetStreamSource(i,&vb,&offset,&stride),"state stream");check(d->GetStreamSourceFreq(i,&freq),"state freq");object(vb);add(offset);add(stride);add(freq);}
        IDirect3DIndexBuffer9* ib=nullptr;check(d->GetIndices(&ib),"state indices");object(ib);
        for(UINT i=0;i<20;++i){const UINT sampler=i<16?i:D3DVERTEXTEXTURESAMPLER0+i-16;IDirect3DBaseTexture9* t=nullptr;check(d->GetTexture(sampler,&t),"state texture");object(t);
            for(UINT k=1;k<=13;++k){DWORD value=0;add(d->GetSamplerState(sampler,D3DSAMPLERSTATETYPE(k),&value));add(value);}}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_VERTEXBLEND,D3DRS_FILLMODE,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_MULTISAMPLEMASK,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP}){DWORD value=0;check(d->GetRenderState(state,&value),"state render");add(value);}
        for(UINT i=0;i<8;++i){DWORD value=0;check(d->GetRenderState(D3DRENDERSTATETYPE(D3DRS_WRAP0+i),&value),"state wrap");add(value);}
        for(UINT i=0;i<8;++i)for(auto state:{D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS}){DWORD value=0;check(d->GetTextureStageState(i,state,&value),"state stage");add(value);}
        float pc[168]{},vc[64]{};check(d->GetPixelShaderConstantF(0,pc,42),"state ps constants");check(d->GetVertexShaderConstantF(0,vc,16),"state vs constants");add(pc);add(vc);
    }
    bool operator==(const Snapshot& o)const{return bytes==o.bytes;}
};

struct Scene{
    Device d;D3DCAPS9 caps;UINT w,h;
    Com<IDirect3DTexture9> depth,target,aux,spare,atlas_copy;Com<IDirect3DSurface9> target_surface,aux_surface,atlas_copy_surface,z;
    Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;Com<IDirect3DTexture9> scene_sys;
    std::vector<float> depth_values;std::uint16_t colour[4];
    Scene(Device device,const D3DCAPS9& c,UINT width,UINT height):d(device),caps(c),w(width),h(height),depth_values(std::size_t(width)*height*4,0.f){
        const float rgba[4]={.25f,.5f,.75f,.37f};for(int i=0;i<4;++i)colour[i]=float_to_half_rne(rgba[i]);
        for(std::size_t i=0;i<std::size_t(w)*h;++i)depth_values[4*i]=2.f; // sky
    }
    void create(){
        upload(d,w,h,D3DFMT_A32B32G32R32F,16,depth_values.data(),0,&depth.p);
        std::vector<std::uint16_t> scene(std::size_t(w)*h*4);for(std::size_t i=0;i<std::size_t(w)*h;++i)std::memcpy(&scene[4*i],colour,8);
        check(d->CreateTexture(w,h,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&scene_sys.p,nullptr),"scene sys");
        D3DLOCKED_RECT lock{};check(scene_sys->LockRect(0,&lock,nullptr,0),"scene lock");for(UINT y=0;y<h;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,scene.data()+std::size_t(y)*w*4,std::size_t(w)*8);check(scene_sys->UnlockRect(0),"scene unlock");
        check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&target.p,nullptr),"target");check(target->GetSurfaceLevel(0,&target_surface.p),"target surface");
        check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&aux.p,nullptr),"aux");check(aux->GetSurfaceLevel(0,&aux_surface.p),"aux surface");
        check(d->CreateTexture(4,4,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&spare.p,nullptr),"spare");
        check(d->CreateTexture(kAtlasWidth,kAtlasHeight,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&atlas_copy.p,nullptr),"atlas copy");check(atlas_copy->GetSurfaceLevel(0,&atlas_copy_surface.p),"atlas copy surface");
        check(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&z.p,nullptr),"depth stencil");
        check(d->CreateVertexBuffer(128,0,0,D3DPOOL_DEFAULT,&vb.p,nullptr),"vb");check(d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib.p,nullptr),"ib");
        refill();
    }
    void release(){unbind();depth.reset();target_surface.reset();target.reset();aux_surface.reset();aux.reset();spare.reset();atlas_copy_surface.reset();atlas_copy.reset();z.reset();vb.reset();ib.reset();scene_sys.reset();}
    void set_depth(const std::vector<float>& values){depth_values=values;depth.reset();upload(d,w,h,D3DFMT_A32B32G32R32F,16,depth_values.data(),0,&depth.p);}
    void refill(){check(scene_sys->AddDirtyRect(nullptr),"scene dirty");check(d->UpdateTexture(scene_sys.p,target.p),"scene refill");}
    void unbind(){
        for(UINT i=0;i<16;++i)d->SetTexture(i,nullptr);for(UINT i=0;i<4;++i)d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr);
        d->SetDepthStencilSurface(nullptr);for(UINT i=1;i<caps.NumSimultaneousRTs;++i)d->SetRenderTarget(i,nullptr);
        Com<IDirect3DSurface9> back;if(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p)))d->SetRenderTarget(0,back.p);
        d->SetIndices(nullptr);for(UINT i=0;i<caps.MaxStreams;++i){d->SetStreamSource(i,nullptr,0,0);d->SetStreamSourceFreq(i,1);}
        d->SetPixelShader(nullptr);d->SetVertexShader(nullptr);d->SetVertexDeclaration(nullptr);
    }
    // Everything the pass touches (8 pixel samplers, c0..c41: the stored path uploads 42 rows, targets, streams) set to values it must put back.
    void hostile(){
        check(d->SetRenderTarget(0,target_surface.p),"hostile rt0");if(caps.NumSimultaneousRTs>1)check(d->SetRenderTarget(1,aux_surface.p),"hostile rt1");check(d->SetDepthStencilSurface(z.p),"hostile depth");
        for(UINT i=0;i<16;++i)check(d->SetTexture(i,spare.p),"hostile texture");for(UINT i=0;i<4;++i)check(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,spare.p),"hostile vertex texture");
        check(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE),"hostile fvf");check(d->SetIndices(ib.p),"hostile index");
        for(UINT i=0;i<caps.MaxStreams;++i){check(d->SetStreamSource(i,vb.p,4,16),"hostile stream");check(d->SetStreamSourceFreq(i,1),"hostile freq");}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_DITHERENABLE})check(d->SetRenderState(state,TRUE),"hostile render");
        check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW),"hostile cull");check(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME),"hostile fill");check(d->SetRenderState(D3DRS_COLORWRITEENABLE,1),"hostile mask");check(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0),"hostile sample mask");
        for(UINT i=0;i<8;++i)check(d->SetRenderState(D3DRENDERSTATETYPE(D3DRS_WRAP0+i),D3DWRAP_U|D3DWRAP_V),"hostile wrap");
        RECT sc{3,5,13,17};check(d->SetScissorRect(&sc),"hostile scissor");D3DVIEWPORT9 vp{2,3,19,13,.2f,.8f};check(d->SetViewport(&vp),"hostile viewport");
        for(UINT i=0;i<16;++i){
            for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})check(d->SetSamplerState(i,state,D3DTADDRESS_WRAP),"hostile address");
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})check(d->SetSamplerState(i,state,i==1||i==7?D3DTEXF_POINT:D3DTEXF_LINEAR),"hostile filter");
            float bias=-.75f;DWORD bits=0;std::memcpy(&bits,&bias,4);check(d->SetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,bits),"hostile LOD");check(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,TRUE),"hostile sRGB");check(d->SetSamplerState(i,D3DSAMP_MAXMIPLEVEL,2),"hostile maxmip");
        }
        float constants[168];for(unsigned i=0;i<168;++i)constants[i]=float(i)*.25f-7;
        check(d->SetPixelShaderConstantF(0,constants,42),"hostile ps constants");check(d->SetVertexShaderConstantF(0,constants,16),"hostile vs constants");
        // The dust motes' stage blends (fog-dust-motes.md): a blend operation and factors it must set and put back.
        check(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_MAX),"hostile blend op");check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR),"hostile src blend");check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"hostile dest blend");
    }
};
FogFrame make_frame(const Case& c,Scene& s,std::uint64_t frame,bool density){
    FogFrame f;f.depth_share=s.depth.p;f.target=s.target_surface.p;f.width=s.w;f.height=s.h;f.frame=frame;
    f.main_target=f.linear_depth_current=f.caller_scene_known=f.caller_queries_idle=true;f.caller_scene_open=false;
    f.params.m00=float(double(s.h)/(double(s.w)*kTan30));f.params.m11=float(1.0/kTan30);f.params.m20=float(-1.0/s.w);f.params.m21=float(1.0/s.h);
    f.params.density_scale=1;f.params.decode_exponent=1.f;f.params.world.valid=true;f.params.world.sun_world[0]=1;
    for(int i=0;i<3;++i){f.params.world.inverse_columns[3*i]=float(c.r[i]);f.params.world.inverse_columns[3*i+1]=float(c.u[i]);f.params.world.inverse_columns[3*i+2]=float(c.f[i]);f.camera_world[i]=c.cam[i];}
    f.density=density;return f;
}
// Unnormalised view ray through raster pixel (px,py): the point RT2 texel (px,py) was rasterised
// at, from the raster offsets alone (m20/m21 minus the pass contract's +1/W, -1/H quad term).
void raster_ray(const FogFrame& f,double px,double py,double view[3]){
    const double raster_x=double(f.params.m20)-1.0/f.width,raster_y=double(f.params.m21)+1.0/f.height;
    view[0]=(2.0*px/f.width-1.0-raster_x)/double(f.params.m00);view[1]=(1.0-2.0*py/f.height-raster_y)/double(f.params.m11);view[2]=1;
}
// The look rows every stored draw binds, at the bin centres (the frames below leave look_resolved false,
// so the shaft lookup offset is dropped and the twin needs no pixel noise), and the sigma factor with them.
float cpu_look_rows[x3m::renderer::fog_look_rows][4];
fog_cpu::Setup cpu_setup(const FogFrame& f,const FogDensityConfig& config){
    fog_cpu::Setup s;for(int a=0;a<3;++a){s.camera[a]=f.camera_world[a];s.chroma[a]=config.chroma[a];for(int b=0;b<3;++b)s.inverse[a][b]=f.params.world.inverse_columns[3*a+b];}
    s.sigma=double(config.sigma);s.offset={config.world_offset[0],config.world_offset[1],config.world_offset[2]};
    if(f.look_resolved)throw std::runtime_error("cpu twin needs bin centres (look_resolved=false)");
    const float radiance[3]={float(s.radiance[0]),float(s.radiance[1]),float(s.radiance[2])}; // c8.rgb = E/pi of the frame
    s.sigma*=double(x3m::renderer::fog_look_constants(config.look,config.chroma,radiance,f.look_phase,cpu_look_rows,false));
    s.look=cpu_look_rows;s.far_bins=config.far_bins;return s;
}

struct Harness{
    Device d;D3DCAPS9 caps;FogPass& pass;FogDensityConfig config;std::uint64_t frame=0;
    Harness(Device device,const D3DCAPS9& c,FogPass& p,const FogDensityConfig& k):d(device),caps(c),pass(p),config(k){}
    std::vector<double> prepare_upload_us,prepare_idle_us;unsigned max_upload_bytes=0,max_upload_rects=0,upload_calls=0;double first_prepare_us=0,upload_us_total=0;
    std::map<std::string,std::vector<std::uint8_t>> static_cache;double static_seconds=0;std::uint64_t static_nodes=0;
    HRESULT prepare(const double cam[3]){
        const unsigned calls=update_calls;const LONGLONG start=ticks();const HRESULT hr=pass.prepare_density(config,cam,++frame);const double us=seconds(start)*1e6;
        const auto& st=pass.density_status();
        if(!first_prepare_us)first_prepare_us=us; // programs, textures, caches and the worker are created here
        else{(st.upload_rects?prepare_upload_us:prepare_idle_us).push_back(us);if(st.upload_rects){upload_us_total+=us;upload_calls+=update_calls-calls;}}
        max_upload_bytes=std::max(max_upload_bytes,st.upload_bytes);max_upload_rects=std::max(max_upload_rects,update_calls-calls);return hr;
    }
    // Pumps prepare_density until both windows are complete, resident and ramped.
    bool settle(const double cam[3],double timeout=180){
        const LONGLONG start=ticks();
        for(;;){
            const HRESULT hr=prepare(cam);if(FAILED(hr)){std::printf("SETTLE prepare hr=%08lx\n",(unsigned long)hr);return false;}
            const auto* cache=pass.fixture_density_cache();const auto& st=pass.density_status();
            if(st.ready_fine==1&&st.ready_far==1&&!cache->has_work()&&cache->gpu_box(0).nodes()==2097152&&cache->gpu_box(1).nodes()==2097152&&
               !retarget_needed(0,origin(0),cam)&&!retarget_needed(1,origin(1),cam))return true;
            if(seconds(start)>timeout)return false;Sleep(1);
        }
    }
    NodeKey origin(int level)const{const NodeBox b=pass.fixture_density_cache()->gpu_box(level);return {b.lo[0],b.lo[1],b.lo[2]};}
    const std::vector<std::uint8_t>& static_atlas(int level,const NodeKey& o){
        char key[128];std::snprintf(key,sizeof key,"%d:%lld:%lld:%lld:%.1f",level,(long long)o.x,(long long)o.y,(long long)o.z,config.world_offset[0]);
        auto found=static_cache.find(key);if(found!=static_cache.end())return found->second;
        std::vector<std::uint8_t> out(kAtlasBytes,0);const LONGLONG start=ticks();const WorldOffset offset{config.world_offset[0],config.world_offset[1],config.world_offset[2]};
        for(int g=0;g<kGroupCount;++g)generate_tile(kLevelDelta[level],o,offset,g,out.data()+std::size_t(g/kGroupsPerRow)*kTileTexels*kAtlasPitch+std::size_t(g%kGroupsPerRow)*kTileTexels*kTexelBytes,kAtlasPitch);
        static_seconds+=seconds(start);static_nodes+=2097152;return static_cache.emplace(key,std::move(out)).first->second;
    }
    // DEFAULT atlas bytes (StretchRect to a render target, then GetRenderTargetData) against a from-scratch fill.
    bool atlases_equal_static(Scene& s,const char* label){
        bool same=true;
        for(int level=0;level<kLevelCount;++level){
            Com<IDirect3DSurface9> source;check(pass.fixture_density_atlas(level)->GetSurfaceLevel(0,&source.p),"atlas surface");
            check(d->StretchRect(source.p,nullptr,s.atlas_copy_surface.p,nullptr,D3DTEXF_NONE),"atlas copy");
            const auto gpu=surface_bytes(d,s.atlas_copy_surface.p);const auto& expect=static_atlas(level,origin(level));
            std::size_t differing=0;for(std::size_t i=0;i<kAtlasBytes;++i)differing+=gpu[i]!=expect[i];
            std::printf("ATLAS %s level=%d differing_bytes=%u\n",label,level,unsigned(differing));same=same&&differing==0;
        }
        return same;
    }
    // One density transaction under hostile caller state; counts a restoration when the state came back byte for byte.
    HRESULT execute(const Case& c,Scene& s,FogResult& r,bool scene_open=false,const FogFrame* custom=nullptr){
        s.refill();s.hostile();if(scene_open)check(d->BeginScene(),"caller begin");
        FogFrame f=custom?*custom:make_frame(c,s,frame,true);f.frame=frame;f.caller_scene_open=scene_open;
        const Snapshot before(d,caps);const HRESULT hr=pass.execute(f,&r);const Snapshot after(d,caps);
        if(scene_open)check(d->EndScene(),"caller end");
        if(before==after&&r.caller_state_restored)++state_restorations;else{++failures;std::printf("STATE_DIFF execute hr=%08lx restored=%u\n",(unsigned long)hr,unsigned(r.caller_state_restored));}
        s.unbind();return hr;
    }
};

// --gpu-sync-timing's marks as FogPass::execute calls them (fog-gpu-cost.md step A): pair counts per pass, and the
// repair-pixel census bracket driving a real occlusion query, as GpuSyncTiming does.
// Step C: the needs-repair bracket on a second query, drawn only while `wanted` (GpuSyncTiming::needs_wanted's role).
struct CensusMarks final:x3m::gpu_sync_timing::Marks{
    IDirect3DQuery9* query=nullptr;unsigned begins[32]{},ends[32]{},census_begins=0,census_ends=0;std::uint32_t area=0;HRESULT issue=S_OK;
    IDirect3DQuery9* needs_query=nullptr;bool wanted=false;unsigned needs_begins=0,needs_ends=0;std::uint32_t needs_area=0,needs_scale=0;
    void begin(unsigned pass)noexcept override{if(pass<32)++begins[pass];}
    void end(unsigned pass)noexcept override{if(pass<32)++ends[pass];}
    void census_begin()noexcept override{++census_begins;const HRESULT hr=query->Issue(D3DISSUE_BEGIN);if(FAILED(hr))issue=hr;}
    void census_end(std::uint32_t a)noexcept override{++census_ends;area=a;const HRESULT hr=query->Issue(D3DISSUE_END);if(FAILED(hr))issue=hr;}
    bool needs_wanted()noexcept override{return wanted&&needs_query;}
    void needs_begin()noexcept override{++needs_begins;const HRESULT hr=needs_query->Issue(D3DISSUE_BEGIN);if(FAILED(hr))issue=hr;}
    void needs_end(std::uint32_t a,std::uint32_t s)noexcept override{++needs_ends;needs_area=a;needs_scale=s;const HRESULT hr=needs_query->Issue(D3DISSUE_END);if(FAILED(hr))issue=hr;}
};

std::vector<Case> read_cases(const std::string& file,FogDensityConfig& config){
    std::ifstream in(file);if(!in)throw std::runtime_error("cases");std::vector<Case> cases;std::string line;
    while(std::getline(in,line)){
        std::istringstream row(line);std::string head;if(!(row>>head))continue;
        if(head=="sigma"){row>>config.sigma;continue;}
        if(head=="chroma"){row>>config.chroma[0]>>config.chroma[1]>>config.chroma[2];continue;}
        Case c;for(double* v:{c.cam,c.r,c.u,c.f})for(int i=0;i<3;++i)row>>v[i];
        if(!row)throw std::runtime_error("case row "+head);if(head=="A_sky"||head=="B_sky")cases.push_back(c);
    }
    if(cases.size()!=2||!(config.sigma>0))throw std::runtime_error("cases need A_sky, B_sky and sigma");return cases;
}

// --- Dust motes (docs/architecture/fog-dust-motes.md): the transaction's last stage against fog_dust_motes_cpu.h ---
// Every mote frame is paired with the same pose drawn with the toggle off (the pass's own launch-off transaction), so
// the difference is the motes alone; the twin predicts it per pixel. Frames run under hostile caller state (Harness).
void invert3(const double m[3][3],double out[3][3]){
    const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    out[0][0]=(m[1][1]*m[2][2]-m[1][2]*m[2][1])/det;out[0][1]=(m[0][2]*m[2][1]-m[0][1]*m[2][2])/det;out[0][2]=(m[0][1]*m[1][2]-m[0][2]*m[1][1])/det;
    out[1][0]=(m[1][2]*m[2][0]-m[1][0]*m[2][2])/det;out[1][1]=(m[0][0]*m[2][2]-m[0][2]*m[2][0])/det;out[1][2]=(m[0][2]*m[1][0]-m[0][0]*m[1][2])/det;
    out[2][0]=(m[1][0]*m[2][1]-m[1][1]*m[2][0])/det;out[2][1]=(m[0][1]*m[2][0]-m[0][0]*m[2][1])/det;out[2][2]=(m[0][0]*m[1][1]-m[0][1]*m[1][0])/det;
}
void dust_motes(Device d,const D3DCAPS9& caps,D3DPRESENT_PARAMETERS& pp,const FogDensityConfig& base,const Case& A){
    using fog_motes_cpu::Mote;
    auto device_refs=[&]{d->AddRef();return unsigned(d->Release());};const unsigned refs_before=device_refs();
    FogDensityConfig config=base;FogMoteTuning& t=config.motes;
    t.count=512;t.size=4;t.streak=8;t.radius=200;t.near_fade=5;t.max_px=12;t.gain=1;t.soft=.02f;t.drift=20;t.seed=7;config.dust_motes=true;
    const double seconds=3.25;const UINT W=128,H=72;
    auto frame_of=[&](const Case& c,Scene& s,std::uint64_t n){FogFrame f=make_frame(c,s,n,true);f.mote_seconds=seconds;return f;};
    auto twin_of=[&](const FogFrame& f,double visibility){
        fog_motes_cpu::Frame out;double m[3][3];
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)m[row][col]=f.params.world.inverse_columns[3*col+row];
        invert3(m,out.rotation);for(int a=0;a<3;++a)out.camera[a]=f.camera_world[a];
        out.m00=f.params.m00;out.m11=f.params.m11;out.m20=double(f.params.m20-1.f/float(f.width));out.m21=double(f.params.m21+1.f/float(f.height));
        out.width=f.width;out.height=f.height;out.seconds=f.mote_seconds;out.brightness=double(t.gain)*double(f.params.density_scale);out.visibility=visibility;
        return out;
    };
    auto list_of=[&](const fog_motes_cpu::Frame& tf,const FogFrame& f){return fog_motes_cpu::motes(t,tf,cpu_setup(f,config));};
    auto on_screen=[&](const Mote& m){int b[4];fog_motes_cpu::bounds(m,b);return m.drawn&&b[2]>=0&&b[3]>=0&&b[0]<int(W)&&b[1]<int(H);};
    auto significant=[&](const Mote& m){return m.drawn&&m.rho>0&&std::max({m.colour[0],m.colour[1],m.colour[2]})>.02;};
    // Motes whose capsule box (one pixel of margin) lies inside the target and meets no other contributing mote's box.
    auto isolated=[&](const std::vector<Mote>& list){
        std::vector<const Mote*> out;
        for(const Mote& m:list){
            if(!significant(m))continue;int b[4];fog_motes_cpu::bounds(m,b);
            if(b[0]<1||b[1]<1||b[2]>int(W)-2||b[3]>int(H)-2)continue;
            bool alone=true;
            for(const Mote& o:list){if(&o==&m||!o.drawn||!(o.rho>0))continue;int c[4];fog_motes_cpu::bounds(o,c);if(c[0]<=b[2]+2&&c[2]>=b[0]-2&&c[1]<=b[3]+2&&c[3]>=b[1]-2){alone=false;break;}}
            if(alone)out.push_back(&m);
        }
        return out;
    };
    auto target=[&](Scene& s){return half_image(surface_bytes(d,s.target_surface.p));};
    auto difference=[&](const std::vector<float>& on,const std::vector<float>& off){std::vector<double> out(std::size_t(W)*H*3);for(std::size_t i=0;i<std::size_t(W)*H;++i)for(int c=0;c<3;++c)out[i*3+c]=double(on[i*4+c])-double(off[i*4+c]);return out;};
    struct Versus{double worst=0,worst_abs=0;unsigned lit=0,alpha_moved=0;};
    auto versus=[&](const std::vector<double>& gpu,const std::vector<double>& ref){Versus v;for(std::size_t i=0;i<gpu.size();++i){const double e=std::fabs(gpu[i]-ref[i]);v.worst_abs=std::max(v.worst_abs,e);v.worst=std::max(v.worst,e/(2e-3+.03*std::fabs(ref[i])));v.lit+=ref[i]>1e-3;}return v;};
    auto moment=[&](const std::vector<double>& img,const Mote& m,double& cx,double& cy){
        int b[4];fog_motes_cpu::bounds(m,b);double sum=0,x=0,y=0;
        for(int py=b[1]-1;py<=b[3]+1;++py)for(int px=b[0]-1;px<=b[2]+1;++px){const std::size_t i=(std::size_t(py)*W+px)*3;const double w=img[i]+img[i+1]+img[i+2];sum+=w;x+=w*px;y+=w*py;}
        cx=sum>0?x/sum:0;cy=sum>0?y/sum:0;return sum;
    };
    Com<IDirect3DTexture9> dark_texture;{const std::vector<float> dark(64*64,0.f);upload(d,64,64,D3DFMT_R32F,4,dark.data(),0,&dark_texture.p);}
    auto shade=[&](Harness& h,Scene& s,const Case& c,bool on,IDirect3DTexture9* map,FogResult& r,bool cut=false){
        h.config.dust_motes=on;check(h.prepare(c.cam),"motes prepare");FogFrame f=frame_of(c,s,h.frame);f.mote_cut=cut;
        if(map){f.count=1;auto& k=f.cascades[0];k.map=map;k.valid=true;k.frame=h.frame;k.bias=0;k.rows[0]=1e-9f;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;k.texel_world=36.6f;k.depth_range=200000.f;}
        if(h.execute(c,s,r,false,&f)!=S_OK||!r.applied)throw std::runtime_error("motes transaction");
        return target(s);
    };
    // Poses: a dense one (most on-screen motes inside cloud) and a void one (every on-screen mote below the coverage
    // with margin), searched with the twin on a 1500-unit lattice of +-18 km around pose A: a 27-point screen of the
    // camera's neighbourhood first, then the full mote list of the candidates.
    Scene scene(d,caps,W,H);scene.create();
    Case dense=A,empty=A;int dense_score=-1;bool found_void=false;
    {
        std::vector<std::pair<int,Case>> candidates;std::vector<Case> clear_candidates;
        for(int i=0;i<25*25*25;++i){
            Case c=A;c.cam[0]+=1500.*(i%25-12);c.cam[1]+=1500.*(i/25%25-12);c.cam[2]+=1500.*(i/625-12);
            const FogFrame f=frame_of(c,scene,0);const fog_cpu::Setup s=cpu_setup(f,config);int filled=0;bool clear=true;
            for(int n=0;n<27;++n){const double q[3]={100.*(n%3-1),100.*(n/3%3-1),100.*(n/9-1)};double margin=0;filled+=fog_motes_cpu::density(s,q,&margin)>.05;clear=clear&&margin<-.05;}
            candidates.push_back({filled,c});if(clear)clear_candidates.push_back(c);
        }
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        for(std::size_t n=0;n<candidates.size()&&n<8;++n){
            const Case& c=candidates[n].second;const FogFrame f=frame_of(c,scene,0);const auto list=list_of(twin_of(f,1),f);
            int seen=0,filled=0;for(const Mote& m:list)if(on_screen(m)){++seen;filled+=m.rho>.05;}
            if(seen>=10&&filled>dense_score){dense_score=filled;dense=c;}
        }
        for(std::size_t n=0;n<clear_candidates.size()&&n<16&&!found_void;++n){
            const Case& c=clear_candidates[n];const FogFrame f=frame_of(c,scene,0);const auto list=list_of(twin_of(f,1),f);
            int seen=0;bool clear=true;for(const Mote& m:list)if(on_screen(m)){++seen;clear=clear&&m.margin<-.02;}
            if(seen>=10&&clear){found_void=true;empty=c;}
        }
    }
    std::printf("MOTES_POSES dense_filled=%d void_found=%u dense_offset=%.0f,%.0f,%.0f void_offset=%.0f,%.0f,%.0f\n",dense_score,unsigned(found_void),
                dense.cam[0]-A.cam[0],dense.cam[1]-A.cam[1],dense.cam[2]-A.cam[2],empty.cam[0]-A.cam[0],empty.cam[1]-A.cam[1],empty.cam[2]-A.cam[2]);
    require(dense_score>=8&&found_void,"M_motes_poses_found");
    // The launch-off reference: the same pose through a pass without the option (the accepted transaction).
    std::vector<float> plain_image;unsigned plain_calls=0;
    {
        FogPass plain;check(plain.attach(d,table,caps,D3DFMT_X8R8G8B8),"plain attach");Harness hp(d,caps,plain,base);check(plain.prepare_targets(W,H),"plain targets");
        require(hp.settle(dense.cam),"motes plain pose settles");FogResult r;check(hp.prepare(dense.cam),"plain prepare");FogFrame f=frame_of(dense,scene,hp.frame);
        if(hp.execute(dense,scene,r,false,&f)!=S_OK||!r.applied||r.motes)throw std::runtime_error("plain transaction");
        plain_image=target(scene);plain_calls=r.device_calls;
        require(!plain.fixture_mote_vertices()&&plain.mote_report().frame==~std::uint64_t(0)&&!plain.motes_variant(),"M_motes_absent_create_and_report_nothing");
        plain.detach();
    }
    FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"motes attach");Harness hx(d,caps,pass,config);check(pass.prepare_targets(W,H),"motes targets");
    const unsigned references_off=pass.references();
    require(hx.settle(dense.cam)&&pass.fixture_mote_vertices()&&pass.motes_variant()&&!pass.density_status().motes_refused,"M_motes_created_at_prepare");
    std::printf("MOTES_RESOURCES count=%u vb_bytes=%u ib_bytes=%u default_vb_bytes_2048=%u default_ib_bytes_2048=%u\n",t.count,t.count*4u*fog_mote_vertex_bytes,t.count*12u,2048u*4u*fog_mote_vertex_bytes,2048u*12u);
    (void)references_off;
    // Off (toggled) and on at the dense pose, sky depth.
    const unsigned allocations=pass.allocations(),references=pass.references();IDirect3DVertexBuffer9* const vb=pass.fixture_mote_vertices();
    FogResult r_off,r_on;const auto off=shade(hx,scene,dense,false,nullptr,r_off);const auto on=shade(hx,scene,dense,true,nullptr,r_on);
    const FogMoteReport report=pass.mote_report();
    require(off==plain_image&&r_off.device_calls==plain_calls&&!r_off.motes,"motes_off_bit_identical");
    require(r_on.motes&&report.drawn&&report.frame==hx.frame&&report.count==t.count&&int(r_on.device_calls)-int(r_off.device_calls)==int(report.calls),"M_motes_calls_equal_on_minus_off");
    std::printf("MOTES_CALLS off=%u on=%u stage=%u shadow=%s\n",r_off.device_calls,r_on.device_calls,report.calls,report.shadow);
    require(pass.allocations()==allocations&&pass.references()==references&&pass.fixture_mote_vertices()==vb,"M_motes_toggle_creates_nothing");
    bool alpha_same=true;for(std::size_t i=3;i<on.size();i+=4)alpha_same=alpha_same&&on[i]==off[i];
    const FogFrame f_dense=frame_of(dense,scene,0);const auto sky_list=list_of(twin_of(f_dense,1),f_dense);
    const auto sky_gpu=difference(on,off);const auto sky_ref=fog_motes_cpu::image(sky_list,W,H,nullptr,t.soft);const Versus sky=versus(sky_gpu,sky_ref);
    double worst_centroid=0,worst_energy=0,worst_twin_centroid=0;unsigned blobs=0;
    for(const Mote* m:isolated(sky_list)){
        double gx,gy,hx_,hy;const double ge=moment(sky_gpu,*m,gx,gy),he=moment(sky_ref,*m,hx_,hy);if(!(he>1e-3))continue;++blobs;
        worst_centroid=std::max(worst_centroid,std::hypot(gx-m->centre[0],gy-m->centre[1]));worst_twin_centroid=std::max(worst_twin_centroid,std::hypot(gx-hx_,gy-hy));
        worst_energy=std::max(worst_energy,std::fabs(ge/he-1));
    }
    unsigned drawn=0,visible=0;for(const Mote& m:sky_list){drawn+=m.drawn;visible+=on_screen(m)&&m.rho>0;}
    std::printf("MOTES_SKY drawn=%u visible_in_fog=%u lit_pixels=%u worst_vs_twin=%.4f worst_abs=%.6f isolated=%u worst_centroid_px=%.4f worst_centroid_vs_twin_px=%.4f worst_energy=%.5f\n",
                drawn,visible,sky.lit,sky.worst,sky.worst_abs,blobs,worst_centroid,worst_twin_centroid,worst_energy);
    require(sky.worst<=1&&sky.lit>=40&&alpha_same,"M_motes_sky_matches_host_twin");
    require(blobs>=3&&worst_centroid<=.5,"M_motes_sky_centroids_within_half_pixel");
    require(blobs>=3&&worst_energy<=.02,"M_motes_sky_energy_within_2_percent");
    // Streak: the next consecutive frame 12 units to the right; then a still frame at the same pose after a gap.
    Case moved=dense;for(int a=0;a<3;++a)moved.cam[a]+=12.*A.r[a];
    FogResult r_streak,r_moved_off,r_still;const auto streak=shade(hx,scene,moved,true,nullptr,r_streak);const FogMoteReport streak_report=pass.mote_report();
    const auto moved_off=shade(hx,scene,moved,false,nullptr,r_moved_off);const auto still=shade(hx,scene,moved,true,nullptr,r_still);const FogMoteReport still_report=pass.mote_report();
    const FogFrame f_moved=frame_of(moved,scene,0);
    auto tf=twin_of(f_moved,1);tf.streak=true;for(int a=0;a<3;++a)tf.previous_camera[a]=dense.cam[a];tf.previous_seconds=seconds;{const auto tp=twin_of(f_dense,1);for(int a=0;a<3;++a)for(int b=0;b<3;++b)tf.previous_rotation[a][b]=tp.rotation[a][b];}
    const auto streak_list=list_of(tf,f_moved);const auto still_list=list_of(twin_of(f_moved,1),f_moved);
    const auto streak_gpu=difference(streak,moved_off),still_gpu=difference(still,moved_off);
    const Versus vs_streak=versus(streak_gpu,fog_motes_cpu::image(streak_list,W,H,nullptr,t.soft)),vs_still=versus(still_gpu,fog_motes_cpu::image(still_list,W,H,nullptr,t.soft));
    double worst_length=0;unsigned streaks=0,clamped=0,long_streaks=0;
    {
        const auto a=isolated(streak_list),b=isolated(still_list);
        for(const Mote* m:a){
            const Mote* s=nullptr;for(const Mote* o:b)if(o->index==m->index)s=o;if(!s)continue;
            double sx,sy,tx,ty;if(!(moment(streak_gpu,*m,sx,sy)>1e-3)||!(moment(still_gpu,*s,tx,ty)>1e-3))continue;
            ++streaks;clamped+=m->length>=double(t.streak)-1e-6;long_streaks+=m->length>=2;worst_length=std::max(worst_length,std::fabs(2*std::hypot(sx-tx,sy-ty)-m->length));
        }
    }
    const double expected_shift=12./double(t.radius)*.5*H*double(f_moved.params.m11);
    unsigned clamped_visible=0;for(const Mote& m:streak_list)clamped_visible+=on_screen(m)&&significant(m)&&m.length>=double(t.streak)-1e-6;
    std::printf("MOTES_STREAK report_streak=%u shift_px=%.4f expected=%.4f worst_vs_twin=%.4f still_worst_vs_twin=%.4f isolated=%u long=%u clamped=%u clamped_visible=%u worst_length_px=%.4f still_report_streak=%u\n",
                unsigned(streak_report.streak),double(streak_report.shift_px),expected_shift,vs_streak.worst,vs_still.worst,streaks,long_streaks,clamped,clamped_visible,worst_length,unsigned(still_report.streak));
    // Per pixel the twin covers every streak, the STREAK-clamped ones included; the length is measured on isolated capsules.
    require(streak_report.streak&&std::fabs(streak_report.shift_px-expected_shift)<=1e-3*expected_shift&&vs_streak.worst<=1&&clamped_visible>=1,"M_motes_streak_matches_host_twin");
    require(streaks>=2&&long_streaks>=2&&worst_length<=1,"M_motes_streak_length_within_1px");
    require(!still_report.streak&&vs_still.worst<=1,"M_motes_cut_zero_length");
    {   // A consecutive frame more than R away is a cut as well.
        Case jump=moved;jump.cam[0]+=300.;FogResult r;shade(hx,scene,jump,true,nullptr,r);
        require(r.motes&&!pass.mote_report().streak&&pass.mote_report().shift_px>0,"M_motes_jump_beyond_radius_is_a_cut");
    }
    {   // The caller's cut signal (a view switch, a roll-only cut) inside every geometric bound: 6 units, a 2 degree yaw.
        // Without the signal the same step streaks; with it the frame is the still frame of the twin (zero length).
        Case turned=moved;const double a=2*3.14159265358979/180,c=std::cos(a),sn=std::sin(a);
        for(int i=0;i<3;++i){turned.r[i]=moved.r[i]*c+moved.f[i]*sn;turned.f[i]=moved.f[i]*c-moved.r[i]*sn;turned.cam[i]+=6.*moved.r[i];}
        FogResult r0,r1,r2,r3,r4;shade(hx,scene,moved,true,nullptr,r0);shade(hx,scene,turned,true,nullptr,r1);const bool control=pass.mote_report().streak;
        shade(hx,scene,moved,true,nullptr,r2);const auto cut_on=shade(hx,scene,turned,true,nullptr,r3,true);const FogMoteReport cut_report=pass.mote_report();
        const auto cut_off=shade(hx,scene,turned,false,nullptr,r4);
        const FogFrame f=frame_of(turned,scene,0);const Versus v=versus(difference(cut_on,cut_off),fog_motes_cpu::image(list_of(twin_of(f,1),f),W,H,nullptr,t.soft));
        std::printf("MOTES_CUT control_streak=%u cut_streak=%u shift_px=%.4f worst_vs_still_twin=%.4f lit_pixels=%u\n",unsigned(control),unsigned(cut_report.streak),double(cut_report.shift_px),v.worst,v.lit);
        require(control&&r3.motes&&!cut_report.streak&&cut_report.shift_px>0&&v.worst<=1&&v.lit>=20,"M_motes_caller_cut_draws_zero_length");
    }
    // Void: no mote in fog, the frame is the launch-off frame byte for byte.
    {
        require(hx.settle(empty.cam),"motes void pose settles");FogResult a,b;const auto void_off=shade(hx,scene,empty,false,nullptr,a);const auto void_on=shade(hx,scene,empty,true,nullptr,b);
        require(b.motes&&void_on==void_off,"M_motes_void_image_identical");
    }
    // Depth: a plane at view z 90 over the whole target; motes behind it vanish, in front stay.
    {
        require(hx.settle(dense.cam),"motes dense pose settles again");
        const float plane=90.f;std::vector<float> depth(std::size_t(W)*H*4,0.f);for(std::size_t i=0;i<std::size_t(W)*H;++i){depth[4*i]=.5f;depth[4*i+2]=plane;}
        scene.set_depth(depth);FogResult a,b;const auto plane_off=shade(hx,scene,dense,false,nullptr,a);const auto plane_on=shade(hx,scene,dense,true,nullptr,b);
        const auto gpu=difference(plane_on,plane_off);const auto ref=fog_motes_cpu::image(sky_list,W,H,depth.data(),t.soft);const Versus v=versus(gpu,ref);
        unsigned behind=0,front=0;bool behind_absent=true,front_present=true;
        for(const Mote& m:sky_list){
            if(!on_screen(m)||!(m.rho>0))continue;const int x=int(std::floor(m.centre[0]+.5)),y=int(std::floor(m.centre[1]+.5));if(x<0||y<0||x>=int(W)||y>=int(H))continue;
            const std::size_t i=(std::size_t(y)*W+x)*3;
            if(m.view[2]>plane*(1+t.soft)+1&&ref[i]+ref[i+1]+ref[i+2]==0){++behind;behind_absent=behind_absent&&gpu[i]==0&&gpu[i+1]==0&&gpu[i+2]==0;}
            if(m.view[2]<plane-5&&ref[i+1]>2e-2){++front;front_present=front_present&&gpu[i+1]>0;}
        }
        std::printf("MOTES_DEPTH plane=%.0f behind=%u front=%u worst_vs_twin=%.4f\n",double(plane),behind,front,v.worst);
        require(v.worst<=1,"M_motes_depth3_matches_host_twin");
        require(behind>=1&&front>=1&&behind_absent&&front_present,"M_motes_depth3_behind_absent_front_present");
        std::vector<float> sky_depth(std::size_t(W)*H*4,0.f);for(std::size_t i=0;i<std::size_t(W)*H;++i)sky_depth[4*i]=2.f;scene.set_depth(sky_depth);
    }
    // Wrap: the camera moved by the cube side on each axis places every mote at the same pixel; half a side does not.
    {
        const double side=2.*t.radius;bool same=true;double worst=0;unsigned compared=0;
        for(int axis=0;axis<4;++axis){
            Case c=dense;c.cam[axis<3?axis:0]+=axis<3?side:side/2;require(hx.settle(c.cam),"motes wrap pose settles");
            FogResult a,b;const auto wrap_off=shade(hx,scene,c,false,nullptr,a);const auto wrap_on=shade(hx,scene,c,true,nullptr,b);
            const FogFrame f=frame_of(c,scene,0);const auto list=list_of(twin_of(f,1),f);const auto gpu=difference(wrap_on,wrap_off);const Versus v=versus(gpu,fog_motes_cpu::image(list,W,H,nullptr,t.soft));
            bool placed=true;for(std::size_t i=0;i<list.size();++i)placed=placed&&list[i].drawn==sky_list[i].drawn&&(!list[i].drawn||(list[i].centre[0]==sky_list[i].centre[0]&&list[i].centre[1]==sky_list[i].centre[1]));
            if(axis<3){
                same=same&&placed&&v.worst<=1;
                for(const Mote* m:isolated(list)){const Mote* o=nullptr;for(const Mote* k:isolated(sky_list))if(k->index==m->index)o=k;if(!o)continue;
                    double gx,gy,sx,sy;if(!(moment(gpu,*m,gx,gy)>1e-3)||!(moment(sky_gpu,*o,sx,sy)>1e-3))continue;++compared;worst=std::max(worst,std::hypot(gx-sx,gy-sy));}
            } else {
                std::printf("MOTES_WRAP axes_same=%u compared=%u worst_centroid_shift_px=%.4f half_side_same=%u\n",unsigned(same),compared,worst,unsigned(placed));
                require(same&&compared>=2&&worst<=.05,"M_motes_wrap_one_side_places_every_mote_identically");
                require(!placed&&v.worst<=1,"M_motes_wrap_half_side_moves_the_lattice");
            }
        }
        require(hx.settle(dense.cam),"motes dense pose settles after the wrap");
    }
    // Shafts, in-march variant: the fully shadowed map puts every mote on the .15 floor of the sun term.
    {
        FogResult a,b;const auto dark_off=shade(hx,scene,dense,false,dark_texture.p,a);const auto dark_on=shade(hx,scene,dense,true,dark_texture.p,b);
        const auto list=list_of(twin_of(f_dense,0),f_dense);const auto gpu=difference(dark_on,dark_off);const Versus v=versus(gpu,fog_motes_cpu::image(list,W,H,nullptr,t.soft));
        double darker=0,lighter=0;for(std::size_t i=0;i<gpu.size();++i){darker+=gpu[i];lighter+=sky_gpu[i];}
        std::printf("MOTES_SHAFTS variant=in_march shadow=%s worst_vs_twin=%.4f energy_ratio=%.4f\n",pass.mote_report().shadow,v.worst,lighter>0?darker/lighter:0.);
        require(b.motes&&std::string(pass.mote_report().shadow)=="in_march"&&v.worst<=1&&darker<lighter&&darker>0,"M_motes_shadow_floor_law_in_march");
    }
    // Reset while on: the VB/IB go with the targets and come back once at the next latch; the frame is unchanged.
    {
        dark_texture.reset();const unsigned before=pass.allocations();
        scene.release();pass.before_reset();const HRESULT reset=d->Reset(&pp);pass.after_reset(reset);check(reset,"motes Reset");scene.create();
        require(!pass.fixture_mote_vertices()&&!pass.reset_pending(),"M_motes_reset_releases_the_buffers");
        check(pass.prepare_targets(W,H),"motes targets after Reset");require(hx.settle(dense.cam)&&pass.fixture_mote_vertices(),"M_motes_reset_recreates_at_prepare");
        const unsigned after=pass.allocations();FogResult a,b;const auto again_off=shade(hx,scene,dense,false,nullptr,a);const auto again_on=shade(hx,scene,dense,true,nullptr,b);
        std::printf("MOTES_RESET allocations_before=%u after=%u\n",before,after);
        require(again_on==on&&again_off==off&&pass.allocations()==after&&after==before+4,"M_motes_after_reset_byte_identical_created_once");
        const std::vector<float> dark(64*64,0.f);upload(d,64,64,D3DFMT_R32F,4,dark.data(),0,&dark_texture.p);
    }
    // The grid variant (X3M_FOG_SHADOW_PASS on): the mote program reads the grid; no cascade is lit, the dark map the floor.
    {
        FogDensityConfig gconfig=config;gconfig.shadow_pass=true;
        FogPass grid;check(grid.attach(d,table,caps,D3DFMT_X8R8G8B8),"motes grid attach");Harness hg(d,caps,grid,gconfig);check(grid.prepare_targets(W,H),"motes grid targets");
        require(hg.settle(dense.cam)&&grid.fixture_grid()&&grid.fixture_mote_vertices(),"motes grid pose settles");
        FogResult a,b,c,e;const auto lit_off=shade(hg,scene,dense,false,nullptr,a);const auto lit_on=shade(hg,scene,dense,true,nullptr,b);const char* lit_shadow=grid.mote_report().shadow;
        const auto dark_off=shade(hg,scene,dense,false,dark_texture.p,c);const auto dark_on=shade(hg,scene,dense,true,dark_texture.p,e);const char* dark_shadow=grid.mote_report().shadow;
        const Versus lit=versus(difference(lit_on,lit_off),fog_motes_cpu::image(sky_list,W,H,nullptr,t.soft));
        const Versus dark=versus(difference(dark_on,dark_off),fog_motes_cpu::image(list_of(twin_of(f_dense,0),f_dense),W,H,nullptr,t.soft));
        std::printf("MOTES_SHAFTS variant=grid lit_shadow=%s dark_shadow=%s lit_worst_vs_twin=%.4f dark_worst_vs_twin=%.4f grid_drawn=%u\n",lit_shadow,dark_shadow,lit.worst,dark.worst,unsigned(e.grid));
        require(std::string(lit_shadow)=="none"&&lit.worst<=1&&b.motes,"M_motes_grid_variant_unshadowed_matches_twin");
        require(std::string(dark_shadow)=="grid"&&e.grid&&e.motes&&dark.worst<=1,"M_motes_shadow_floor_law_grid");
        scene.release();grid.detach();require(grid.references()==0,"M_motes_grid_detach_releases_everything");scene.create();
    }
    // A device without an explicit blend operation refuses the mote stage only: the frame is the launch-off one.
    {
        D3DCAPS9 limited=caps;limited.PrimitiveMiscCaps&=~DWORD(D3DPMISCCAPS_BLENDOP);
        FogPass refused;check(refused.attach(d,table,limited,D3DFMT_X8R8G8B8),"motes refused attach");Harness hr(d,caps,refused,config);check(refused.prepare_targets(W,H),"motes refused targets");
        require(hr.settle(dense.cam),"motes refused pose settles");FogResult r;const auto image=shade(hr,scene,dense,true,nullptr,r);
        const char* reason=refused.density_status().motes_refused;
        std::printf("MOTES_REFUSAL reason=%s calls=%u plain_calls=%u\n",reason?reason:"none",r.device_calls,plain_calls);
        require(reason&&std::string(reason)=="mote_blend_caps"&&!refused.fixture_mote_vertices()&&!r.motes&&image==plain_image&&r.device_calls==plain_calls&&!refused.motes_variant(),"M_motes_refused_capability_keeps_the_fog_frame");
        scene.release();refused.detach();scene.create();
    }
    dark_texture.reset();scene.release();pass.detach();
    require(pass.references()==0&&!pass.fixture_mote_vertices(),"M_motes_detach_releases_everything");
    require(device_refs()==refs_before,"M_motes_device_refcount_balanced");
}
void run(const std::string& cases_file){
    FogDensityConfig config;config.enabled=true;config.sector_key=0x5ec7;config.recipe=1;
    const std::vector<Case> cases=read_cases(cases_file,config);const Case& A=cases[0];
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="x3m-fog-density-pass";RegisterClassA(&cls);
    HWND window=CreateWindowA(cls.lpszClassName,"Detached stored-density fog pass",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);if(!window)throw std::runtime_error("window");
    Com<IDirect3D9> api;api.p=Direct3DCreate9(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Direct3DCreate9");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=256;pp.BackBufferHeight=144;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> device;check(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"CreateDevice");Device d=device.p;
    D3DCAPS9 caps{};check(d->GetDeviceCaps(&caps),"caps");
    std::memcpy(table,*reinterpret_cast<void***>(d),sizeof table);real_update=reinterpret_cast<UpdateSurfaceFn>(table[30]);table[30]=reinterpret_cast<void*>(&update_stub);
    real_create_texture=reinterpret_cast<CreateTextureFn>(table[23]);table[23]=reinterpret_cast<void*>(&create_texture_stub);
    real_create_pixel_shader=reinterpret_cast<CreatePixelShaderFn>(table[106]);table[106]=reinterpret_cast<void*>(&create_pixel_shader_stub);
    auto device_references=[&]{d->AddRef();return unsigned(d->Release());};const unsigned references_before=device_references();
    std::vector<std::uint8_t> off_none,off_split; // the in-march programs' no-map and split-map frames, for the visibility grid below
    {
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"attach");Harness hx(d,caps,pass,config);
        Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"targets");
        // --- Off: nothing exists ---
        FogDensityConfig off=config;off.enabled=false;const unsigned references=pass.references(),allocations=pass.allocations();
        require(pass.prepare_density(off,A.cam,1)==S_FALSE&&!pass.fixture_density_cache()&&pass.references()==references&&pass.allocations()==allocations&&update_calls==0&&!pass.density_status().available,"off_no_worker_no_resources_no_device_calls");
        {FogResult r;FogFrame f=make_frame(A,scene,1,true);require(pass.execute(f,&r)==E_INVALIDARG&&r.device_calls==0,"density_frame_refused_before_prepare_without_device_calls");}

        // --- First fill: nothing blocks, ramps are monotone and bounded, T never pops ---
        const LONGLONG fill_start=ticks();double far_ms=0,fine_ms=0;std::vector<std::vector<float>> T;std::vector<float> ready_far,ready_fine;bool executed_ok=true;unsigned no_fog_frames=0;
        {const Snapshot before(d,caps);scene.hostile();const Snapshot hostile_before(d,caps);check(hx.prepare(A.cam),"first prepare");const Snapshot hostile_after(d,caps);require(hostile_before==hostile_after,"prepare_density_leaves_hostile_state_untouched");state_restorations+=hostile_before==hostile_after;scene.unbind();(void)before;}
        for(;;){
            check(hx.prepare(A.cam),"fill prepare");const auto& st=pass.density_status();
            if(!far_ms&&st.ready_far>0)far_ms=seconds(fill_start)*1e3;if(!fine_ms&&st.ready_fine>0)fine_ms=seconds(fill_start)*1e3;
            FogResult r;const HRESULT hr=hx.execute(A,scene,r,(hx.frame&1)!=0);
            if(hr==S_FALSE){++no_fog_frames;executed_ok=executed_ok&&r.device_calls==0&&st.ready_far==0;}
            else{executed_ok=executed_ok&&hr==S_OK&&r.applied;const auto lit=half_image(surface_bytes(d,pass.fixture_st()));std::vector<float> t(lit.size()/4);for(std::size_t i=0;i<t.size();++i)t[i]=lit[4*i+3];T.push_back(std::move(t));ready_far.push_back(st.ready_far);ready_fine.push_back(st.ready_fine);}
            if(st.ready_far==1&&st.ready_fine==1&&!pass.fixture_density_cache()->has_work()&&pass.fixture_density_cache()->gpu_box(0).nodes()==2097152&&pass.fixture_density_cache()->gpu_box(1).nodes()==2097152)break;
            if(seconds(fill_start)>180)throw std::runtime_error("fill timeout");Sleep(1);
        }
        bool monotone=true,bounded=true;for(std::size_t i=1;i<ready_far.size();++i){monotone=monotone&&ready_far[i]>=ready_far[i-1]&&ready_fine[i]>=ready_fine[i-1];bounded=bounded&&ready_far[i]-ready_far[i-1]<=1.f/90+1e-6f&&ready_fine[i]-ready_fine[i-1]<=1.f/90+1e-6f;}
        double tau_max=0,worst_step=0,first_step=0;for(float t:T.back())tau_max=std::max(tau_max,-std::log(double(t)));
        for(std::size_t i=0;i<T.size();++i)for(std::size_t j=0;j<T[i].size();++j){const double step=std::fabs(double(T[i][j])-double(i?T[i-1][j]:1.f));worst_step=std::max(worst_step,step);if(!i)first_step=std::max(first_step,step);}
        // Far ramp: d/dr exp(-r tau) <= tau; fine ramp moves T between two complete fields over 90 frames. Two FP16 steps of slack.
        const double bound=1.5*tau_max/90+2*4.9e-4;const auto fill_status=pass.density_status();
        std::printf("FILL far_first_fog_ms=%.1f fine_first_ms=%.1f total_ms=%.1f frames=%llu no_fog_frames=%u drawn_frames=%u tau_max=%.4f worst_T_step=%.6f first_T_step=%.6f bound=%.6f nodes=%llu worker_busy_ms=%.1f nodes_per_second=%.0f\n",far_ms,fine_ms,seconds(fill_start)*1e3,(unsigned long long)hx.frame,no_fog_frames,unsigned(T.size()),tau_max,worst_step,first_step,bound,
            (unsigned long long)fill_status.nodes_generated,fill_status.worker_busy_us/1e3,fill_status.worker_busy_us?fill_status.nodes_generated*1e6/fill_status.worker_busy_us:0.);
        require(executed_ok&&no_fog_frames>0,"no_fog_frames_are_zero_device_call_frames_then_every_draw_applies");
        require(monotone&&bounded&&ready_far.size()>=90,"readiness_ramps_monotone_at_most_one_ninetieth_per_frame");
        require(tau_max>.05&&worst_step<=bound,"no_pop_per_frame_T_change_bounded");
        require(fill_status.nodes_generated==2ull*2097152,"fill_generates_each_node_once");
        require(hx.atlases_equal_static(scene,"fill"),"dynamic_fill_equals_static_cache_bit_for_bit");
        require(hx.max_upload_bytes<=kDefaultUploadBudget&&hx.max_upload_bytes>0&&hx.max_upload_rects<=kDefaultUploadRects,"upload_budget_respected");

        // --- Production march through the pass against the CPU march (independent of uv / c0 laws) ---
        {
            FogResult r;check(hx.prepare(A.cam),"prepare");require(hx.execute(A,scene,r)==S_OK&&r.applied&&r.half_width==64&&r.half_height==36,"density_transaction_applies");
            const auto lit=half_image(surface_bytes(d,pass.fixture_st()));const FogFrame f=make_frame(A,scene,hx.frame,true);const fog_cpu::Setup cpu=cpu_setup(f,config);double worst_T=0,worst_S=0;
            for(UINT y=1;y<36;y+=7)for(UINT x=2;x<64;x+=9){double view[3];raster_ray(f,2.0*x,2.0*y,view);const auto ref=fog_cpu::march(cpu,view,0);const float* g=&lit[(std::size_t(y)*64+x)*4];
                worst_T=std::max(worst_T,std::fabs(ref.T-g[3]));for(int c=0;c<3;++c)worst_S=std::max(worst_S,std::fabs(ref.S[c]-g[c]));}
            std::printf("PASS_VS_CPU sky_pixels=35 worst_T=%.6f worst_S=%.6f\n",worst_T,worst_S);require(worst_T<=.003&&worst_S<=.003,"pass_march_matches_cpu_reference");
        }
        const std::vector<std::uint8_t> image_before=surface_bytes(d,pass.fixture_st());

        // --- Steady state cost ---
        {
            const auto before=pass.density_status();const unsigned calls=update_calls;const LONGLONG start=ticks();const int n=20000;for(int i=0;i<n;++i)pass.prepare_density(config,A.cam,++hx.frame);
            const double ns=seconds(start)*1e9/n;std::printf("STEADY prepare_density_ns=%.0f frames=%d update_surface_calls=%u\n",ns,n,update_calls-calls);
            require(update_calls==calls&&pass.density_status().nodes_generated==before.nodes_generated&&pass.density_status().upload_bytes==0,"steady_state_no_uploads_no_generation");
        }

        // --- Recentre: origin advances while unchanged pixels stay byte-identical ---
        {
            double cam[3]={A.cam[0],A.cam[1],A.cam[2]};const std::uint64_t nodes_before=pass.density_status().nodes_generated,bytes_before=pass.density_status().upload_bytes_total;const NodeKey origin_before=hx.origin(0);
            for(int a=0;a<3;++a)cam[a]+=2*kFineDelta;
            // The worker retargets for `cam`; the transaction still renders pose A, whose rays stay inside both windows.
            const LONGLONG start=ticks();bool drawn=true;
            for(;;){check(hx.prepare(cam),"recentre prepare");const auto* cache=pass.fixture_density_cache();if(!cache->has_work()&&cache->gpu_box(0).nodes()==2097152&&hx.origin(0).x==origin_before.x+2)break;if(seconds(start)>60)throw std::runtime_error("recentre timeout");Sleep(1);}
            FogResult r;FogFrame f=make_frame(A,scene,hx.frame,true);drawn=hx.execute(A,scene,r,false,&f)==S_OK&&r.applied;
            const NodeKey origin_after=hx.origin(0);const auto image_after=surface_bytes(d,pass.fixture_st());
            std::printf("RECENTRE origin_x=%lld->%lld nodes=%llu upload_bytes=%llu seconds=%.3f\n",(long long)origin_before.x,(long long)origin_after.x,(unsigned long long)(pass.density_status().nodes_generated-nodes_before),(unsigned long long)(pass.density_status().upload_bytes_total-bytes_before),seconds(start));
            require(origin_after.x==origin_before.x+2&&origin_after.y==origin_before.y+2&&origin_after.z==origin_before.z+2&&pass.density_status().nodes_generated-nodes_before==2097152ull-126ull*126*126,"recentre_generates_only_the_entering_slabs");
            require(drawn&&image_after==image_before,"origin_advance_leaves_unchanged_pixels_byte_identical");
        }
        // --- Camera cut to a window whose origin sits at storage 126: ramps restart, then the seam is crossed ---
        Case R=A;for(int a=0;a<3;++a)R.cam[a]=(std::floor(A.cam[a]/kFineDelta/128)*128+128*40+61+.37)*kFineDelta; // 2.6e6 units away: outside both windows
        {
            check(hx.prepare(R.cam),"cut prepare");require(pass.density_status().ready_far==0&&pass.density_status().ready_fine==0,"camera_cut_drops_readiness_at_once");
            FogResult r;require(hx.execute(R,scene,r)==S_FALSE&&r.device_calls==0,"cut_frame_draws_nothing");
            require(hx.settle(R.cam),"cut_refills_and_ramps");const NodeKey o=hx.origin(0);require(storage_index(o.x)==126&&storage_index(o.y)==126&&storage_index(o.z)==126,"window_origin_at_storage_126");
            bool same=true;std::uint64_t worst_bytes=0;
            for(int move=0;move<3;++move){
                const std::uint64_t nodes=pass.density_status().nodes_generated,bytes=pass.density_status().upload_bytes_total;for(int a=0;a<3;++a)R.cam[a]+=2*kFineDelta;
                require(hx.settle(R.cam),"recentre settles");const std::uint64_t made=pass.density_status().nodes_generated-nodes,sent=pass.density_status().upload_bytes_total-bytes;worst_bytes=std::max(worst_bytes,sent);
                std::printf("SEAM_RECENTRE move=%d origin_storage=%d nodes=%llu upload_bytes=%llu\n",move,storage_index(hx.origin(0).x),(unsigned long long)made,(unsigned long long)sent);
                same=same&&made==2097152ull-126ull*126*126&&hx.atlases_equal_static(scene,"seam");
            }
            require(same,"recentre_across_tile_seam_and_lane_3_to_0_equals_from_scratch_fill");require(worst_bytes<kAtlasBytes,"recentre_uploads_less_than_one_atlas");
        }

        // --- Seam-crossing rays, group 31 lane 3 -> group 0 lane 0, against the CPU march ---
        {
            Case S=A;fog_cpu::Setup best;double best_control=0;std::vector<float> depth(std::size_t(scene.w)*scene.h*4,0.f);
            // Camera 14400 units below a storage seam shared by both levels (a multiple of 128*4096) on all three axes, looking along
            // +x+y+z: the seam is crossed near s = 25000, inside the LOD blend, so both levels read storage 127 -> 0 and lane 3 -> 0.
            const double n=1/std::sqrt(3.0);const double f3[3]={n,n,n},r3[3]={1/std::sqrt(2.0),-1/std::sqrt(2.0),0},u3[3]={n/std::sqrt(2.0),n/std::sqrt(2.0),-2*n/std::sqrt(2.0)};
            for(int a=0;a<3;++a){S.r[a]=r3[a];S.u[a]=u3[a];S.f[a]=f3[a];}
            FogFrame probe=make_frame(S,scene,0,true);double centre[3];raster_ray(probe,scene.w/2,scene.h/2,centre);
            for(int k=-60;k<=60&&best_control<.02;++k){
                Case candidate=S;for(int a=0;a<3;++a)candidate.cam[a]=128.0*kFarDelta*(k+a)-14400.;
                FogFrame f=make_frame(candidate,scene,0,true);fog_cpu::Setup cpu=cpu_setup(f,config);const double depth_b=60000/std::sqrt(centre[0]*centre[0]+centre[1]*centre[1]+1);
                const auto whole=fog_cpu::march(cpu,centre,depth_b);cpu.break_seam=true;const auto broken=fog_cpu::march(cpu,centre,depth_b);
                if(std::fabs(whole.T-broken.T)>best_control){best_control=std::fabs(whole.T-broken.T);S=candidate;}
            }
            for(UINT y=0;y<scene.h;++y)for(UINT x=0;x<scene.w;++x){double v[3];raster_ray(probe,x&~1u,y&~1u,v);float* p=&depth[(std::size_t(y)*scene.w+x)*4];p[0]=.5f;p[2]=float(60000/std::sqrt(v[0]*v[0]+v[1]*v[1]+1));}
            scene.set_depth(depth);require(best_control>.003&&hx.settle(S.cam),"seam_camera_with_density_across_the_seam");
            FogResult r;require(hx.execute(S,scene,r)==S_OK&&r.applied,"seam_transaction_applies");const auto lit=half_image(surface_bytes(d,pass.fixture_st()));
            const FogFrame f=make_frame(S,scene,hx.frame,true);fog_cpu::Setup cpu=cpu_setup(f,config),broken=cpu;broken.break_seam=true;double worst=0,control=0;
            for(UINT y=4;y<36;y+=9)for(UINT x=5;x<64;x+=11){double v[3];raster_ray(f,2.0*x,2.0*y,v);const double depth_b=double(depth[(std::size_t(2*y)*scene.w+2*x)*4+2]);
                const auto ref=fog_cpu::march(cpu,v,depth_b),bad=fog_cpu::march(broken,v,depth_b);const float* g=&lit[(std::size_t(y)*64+x)*4];
                worst=std::max(worst,std::fabs(ref.T-g[3]));control=std::max(control,std::fabs(bad.T-g[3]));for(int c=0;c<3;++c)worst=std::max(worst,std::fabs(ref.S[c]-g[c]));}
            std::printf("SEAM fine_xy_seam=%u fine_lane_wrap=%u far_xy_seam=%u far_lane_wrap=%u worst_vs_cpu=%.6f broken_seam_control=%.6f\n",cpu.seam_xy_samples[0],cpu.lane_wrap_samples[0],cpu.seam_xy_samples[1],cpu.lane_wrap_samples[1],worst,control);
            require(cpu.seam_xy_samples[0]>0&&cpu.lane_wrap_samples[0]>0&&cpu.seam_xy_samples[1]>0&&cpu.lane_wrap_samples[1]>0,"rays_sample_the_tile_seam_and_lane_3_to_0_on_both_levels");
            require(worst<=.003&&control>.003&&control>2*worst,"seam_and_lane_wrap_match_cpu_and_reject_a_broken_wrap");
            std::vector<float> sky(std::size_t(scene.w)*scene.h*4,0.f);for(std::size_t i=0;i<sky.size();i+=4)sky[i]=2.f;scene.set_depth(sky);
        }

        // --- Shafts: fog_visibility through the production march ---
        {
            require(hx.settle(A.cam),"shaft pose settles");const UINT N=64;std::vector<float> lit_map(N*N,1.f),dark_map(N*N,0.f),split_map(N*N);for(UINT y=0;y<N;++y)for(UINT x=0;x<N;++x)split_map[y*N+x]=x<N/2?1.f:0.f;
            Com<IDirect3DTexture9> lit_texture,dark_texture,split_texture;upload(d,N,N,D3DFMT_R32F,4,lit_map.data(),0,&lit_texture.p);upload(d,N,N,D3DFMT_R32F,4,dark_map.data(),0,&dark_texture.p);upload(d,N,N,D3DFMT_R32F,4,split_map.data(),0,&split_texture.p);
            auto shade=[&](IDirect3DTexture9* map,float x_scale){
                check(hx.prepare(A.cam),"shaft prepare");FogFrame f=make_frame(A,scene,hx.frame,true);
                if(map){f.count=1;auto& k=f.cascades[0];k.map=map;k.valid=true;k.frame=hx.frame;k.bias=0;k.rows[0]=x_scale;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;}
                FogResult r;if(hx.execute(A,scene,r,false,&f)!=S_OK||r.cascades_bound!=(map?1u:0u))throw std::runtime_error("shaft transaction");return surface_bytes(d,pass.fixture_st());
            };
            const auto none=shade(nullptr,0),all_lit=shade(lit_texture.p,1e-9f),all_dark=shade(dark_texture.p,1e-9f);const auto split_bytes=shade(split_texture.p,1e-5f);
            off_none=none;off_split=split_bytes;
            require(all_lit==none,"shafts_fully_lit_map_is_bit_identical_to_no_map");
            // The look removes sun light only: a fully shadowed column keeps its transmittance bit for bit and keeps
            // coloured in-scatter (ambient plus the shaft and lift floors), dimmer than the unshadowed one.
            const auto plain=half_image(none),dark=half_image(all_dark),split=half_image(split_bytes);bool dark_ok=true;unsigned fogged=0;
            for(std::size_t i=0;i<plain.size();i+=4){
                dark_ok=dark_ok&&dark[i+3]==plain[i+3]&&dark[i]<=plain[i]&&dark[i+1]<=plain[i+1]&&dark[i+2]<=plain[i+2];
                if(!(dark[i+3]<.98f))continue;++fogged; // FP16 target: in-scatter below the smallest normal half may flush to zero
                dark_ok=dark_ok&&dark[i]>0&&dark[i+1]>0&&dark[i+2]>0&&dark[i+1]<plain[i+1];
            }
            std::printf("SHAFTS dark_map fogged_pixels=%u\n",fogged);
            require(dark_ok&&fogged>0,"shafts_fully_shadowed_dim_coloured_same_transmittance");
            // Split map: map x = 1e-5 * view x, left half lit. CPU twin of fog_pcf / shadow_weight (cascade 0 only).
            const FogFrame f=make_frame(A,scene,hx.frame,true);fog_cpu::Setup cpu=cpu_setup(f,config);
            cpu.visibility=[&](const double p[3]){
                const double px=double(1e-5f)*p[0],py=double(1e-9f)*p[1],pz=double(1e-9f)*p[2]+.5,m=std::max(std::fabs(px),std::fabs(py));
                if(!(m<=double(.95f)&&pz>=0&&pz<=1))return 1.0;const double weight=1-std::min(std::max((m-double(.85f))*10,0.),1.);
                const double tx=px*.5*N+.5*N,ty=-py*.5*N+.5*N,bx=std::floor(tx),by=std::floor(ty),fx=tx-bx,fy=ty-by;
                auto tap=[&](double x,double y){const int ix=std::min(std::max(int(x),0),int(N)-1),iy=std::min(std::max(int(y),0),int(N)-1);return split_map[iy*N+ix]>=pz?1.0:0.0;};
                const double pcf=(tap(bx,by)*(1-fx)+tap(bx+1,by)*fx)*(1-fy)+(tap(bx,by+1)*(1-fx)+tap(bx+1,by+1)*fx)*fy;return std::min(std::max(1-weight*(1-pcf),0.),1.);
            };
            double worst=0;bool differs=false;
            for(UINT y=3;y<36;y+=8)for(UINT x=1;x<64;x+=6){double v[3];raster_ray(f,2.0*x,2.0*y,v);const auto ref=fog_cpu::march(cpu,v,0);const std::size_t at=(std::size_t(y)*64+x)*4;for(int c=0;c<3;++c)worst=std::max(worst,std::fabs(ref.S[c]-split[at+c]));differs=differs||split[at+1]!=plain[at+1];}
            std::printf("SHAFTS split_map worst_S_vs_cpu=%.6f\n",worst);require(worst<=.003&&differs,"shafts_split_map_matches_cpu_visibility");
        }

        // --- The single look through the production pass: prebuilt programs and constants only ---
        std::vector<std::uint8_t> look_before;
        {
            require(hx.settle(A.cam),"look pose settles");
            const UINT N=64;const std::vector<float> dark_map(N*N,0.f);Com<IDirect3DTexture9> dark_texture;upload(d,N,N,D3DFMT_R32F,4,dark_map.data(),0,&dark_texture.p);
            const unsigned references=pass.references(),allocations=pass.allocations();
            auto shade=[&](unsigned phase,bool dark,bool resolved=true){
                check(hx.prepare(A.cam),"look prepare");FogFrame f=make_frame(A,scene,hx.frame,true);f.look_phase=phase;f.look_resolved=resolved;
                if(dark){f.count=1;auto& k=f.cascades[0];k.map=dark_texture.p;k.valid=true;k.frame=hx.frame;k.bias=0;k.rows[0]=1e-9f;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;}
                FogResult r;if(hx.execute(A,scene,r,false,&f)!=S_OK||!r.applied)throw std::runtime_error("look transaction");return surface_bytes(d,pass.fixture_st());
            };
            const auto a=shade(0,false),b=shade(0,false),c3=shade(3,false),c4=shade(4,false),held=shade(3,false,false);
            require(b==a,"look_frames_are_byte_identical_frame_to_frame");
            // With no cascade bound nothing reads the phase: the retired L3 sample offset is gone, so the density
            // samples sit at the bin centres whatever the TAA phase or resolve state is.
            require(c3==a&&c4==a&&held==a,"phase_and_resolve_move_no_density_sample");
            require(pass.references()==references&&pass.allocations()==allocations,"look_frames_create_and_allocate_nothing");
            const auto dark=half_image(shade(0,true));unsigned fogged=0;bool coloured=true;
            for(std::size_t i=0;i<dark.size();i+=4){if(!(dark[i+3]<.98f))continue;++fogged; // FP16 target: in-scatter below the smallest normal half (6.1e-5) may flush to zero
            coloured=coloured&&dark[i]>0&&dark[i+1]>0&&dark[i+2]>0;}
            std::printf("LOOKS shadowed_fogged_pixels=%u coloured=%u\n",fogged,unsigned(coloured));
            require(fogged>0&&coloured,"fully_shadowed_fog_is_coloured");
            look_before=a;
        }

        // --- Reset in the middle of a fill, then Reset of a complete cache ---
        {
            pass.invalidate_density();for(int i=0;i<6;++i){check(hx.prepare(A.cam),"mid-fill prepare");Sleep(2);}
            const std::uint64_t nodes_before=pass.density_status().nodes_generated;const bool mid=pass.fixture_density_cache()->gpu_box(0).nodes()!=2097152;
            scene.release();pass.before_reset();const HRESULT reset=d->Reset(&pp);pass.after_reset(reset);check(reset,"Reset");scene.create();
            require(mid&&!pass.fixture_density_atlas(0)&&!pass.reset_pending(),"reset_mid_fill_released_default_atlases");
            check(pass.prepare_targets(scene.w,scene.h),"targets after Reset");require(hx.settle(A.cam)&&hx.atlases_equal_static(scene,"reset_mid_fill"),"reset_mid_fill_recovers_bit_for_bit");
            require(pass.density_status().nodes_generated-nodes_before<=2ull*2097152,"reset_mid_fill_generates_nothing_twice");
            FogResult r;require(hx.execute(A,scene,r)==S_OK&&r.applied&&surface_bytes(d,pass.fixture_st())==image_before,"after_reset_image_byte_identical_to_before");
            {check(hx.prepare(A.cam),"look prepare after Reset");FogFrame f=make_frame(A,scene,hx.frame,true);FogResult lr;
             require(hx.execute(A,scene,lr,false,&f)==S_OK&&surface_bytes(d,pass.fixture_st())==look_before,"look_after_reset_byte_identical_to_before");}
            const std::uint64_t nodes=pass.density_status().nodes_generated,bytes=pass.density_status().upload_bytes_total;const std::uint64_t frame_before=hx.frame;
            scene.release();pass.before_reset();const HRESULT again=d->Reset(&pp);pass.after_reset(again);check(again,"second Reset");scene.create();check(pass.prepare_targets(scene.w,scene.h),"targets after second Reset");
            check(hx.prepare(A.cam),"prepare after Reset");require(pass.density_status().ready_far==0,"reset_drops_readiness_until_reuploaded");
            require(hx.settle(A.cam)&&hx.atlases_equal_static(scene,"reset_complete"),"reset_of_complete_cache_reuploads_from_cpu");
            std::printf("RESET_REUPLOAD frames=%llu upload_bytes=%llu regenerated_nodes=%llu\n",(unsigned long long)(hx.frame-frame_before),(unsigned long long)(pass.density_status().upload_bytes_total-bytes),(unsigned long long)(pass.density_status().nodes_generated-nodes));
            require(pass.density_status().nodes_generated==nodes&&pass.density_status().upload_bytes_total-bytes==2*kAtlasBytes,"reset_keeps_cpu_data_no_regeneration");
        }
        // --- Injected device loss inside an upload ---
        {
            double cam[3]={A.cam[0]+2*kFineDelta,A.cam[1],A.cam[2]};HRESULT hr=S_OK;lose_updates=1;const LONGLONG start=ticks();
            while(SUCCEEDED(hr)&&seconds(start)<60){hr=hx.prepare(cam);Sleep(1);}
            FogResult r;FogFrame f=make_frame(A,scene,hx.frame,true);const HRESULT refused=pass.execute(f,&r);
            require(!pass.density_status().available&&pass.density_status().ready_far==0,"failed_upload_clears_availability");
            require(hr==D3DERR_DEVICELOST&&pass.reset_pending()&&refused==D3DERR_DEVICENOTRESET&&r.device_calls==0&&pass.prepare_density(config,cam,++hx.frame)==D3DERR_DEVICENOTRESET,"injected_loss_poisons_until_reset_without_device_calls");
            pass.before_reset();pass.after_reset(S_OK);check(pass.prepare_targets(scene.w,scene.h),"targets after loss");
            require(hx.settle(cam)&&hx.atlases_equal_static(scene,"injected_loss"),"injected_loss_recovers_bit_for_bit");
        }
        // --- Cold hand-over (fog-handover.md, "Implementation"): with step and cold fill a cold start uploads the far level in
        // one whole-atlas latch past the budget, then draws at full far readiness in the resident frame; switches off after ---
        {
            const unsigned budget_max=hx.max_upload_bytes; // the case's one oversize latch stays out of the PREPARE_CPU budget row
            hx.config.handover_step=hx.config.handover_coldfill=true;check(hx.prepare(A.cam),"handover prepare");pass.invalidate_density();
            unsigned oversize=0,whole=0,early=0,frames=0;float before=0;bool stepped=false;const LONGLONG start=ticks();
            while(seconds(start)<60){
                const HRESULT hr=hx.prepare(A.cam);if(FAILED(hr))break;++frames;const auto& st=pass.density_status();
                if(st.upload_bytes>kDefaultUploadBudget){++oversize;whole+=st.upload_bytes==kAtlasBytes&&st.upload_rects==1;}
                else if(st.upload_bytes&&!oversize)++early; // anything before the whole-atlas latch
                if(st.ready_far>0){stepped=before==0&&st.ready_far==1;break;}
                before=st.ready_far;Sleep(1);
            }
            const auto h=pass.density_status().handover;
            std::printf("HANDOVER frames=%u oversize=%u whole=%u early=%u latches=%u upload_bytes=%llu ready_ms=%.1f drawable_ms=%.1f fill_ms=%.1f fill_busy_ms=%.1f fill_cpu_ms=%.1f\n",frames,oversize,whole,early,h.latches,
                (unsigned long long)h.upload_bytes,double(std::int32_t(h.ready_us/100))*.1,double(std::int32_t(h.drawable_us/100))*.1,double(std::int32_t(h.fill_us/100))*.1,double(std::int32_t(h.fill_busy_us/100))*.1,double(std::int32_t(h.fill_cpu_us/100))*.1);
            require(stepped,"cold_handover_far_readiness_steps_from_0_to_1");
            require(oversize==1&&whole==1&&early==0,"cold_fill_one_whole_atlas_latch_past_the_budget");
            require(h.due&&h.step&&h.cold_fill&&h.whole_atlas&&h.latches==1&&h.upload_bytes==kAtlasBytes&&h.ready_frame==h.drawable_frame,"cold_handover_report_in_the_resident_frame");
            FogResult r;require(hx.execute(A,scene,r)==S_OK&&r.applied,"cold_handover_draws_in_the_resident_frame");
            require(hx.settle(A.cam)&&hx.atlases_equal_static(scene,"cold_handover"),"cold_handover_settles_bit_for_bit");
            hx.config.handover_step=hx.config.handover_coldfill=false;hx.max_upload_bytes=budget_max;
        }

        // --- Odd target, repair and composite against CPU references ---
        {
            Scene odd(d,caps,31,17);std::vector<float> depth(std::size_t(31)*17*4,0.f);FogFrame shape=make_frame(A,odd,0,true);
            for(UINT y=0;y<17;++y)for(UINT x=0;x<31;++x){float* p=&depth[(std::size_t(y)*31+x)*4];if(x%2){double v[3];raster_ray(shape,x,y,v);p[0]=.5f;p[2]=float(60000/std::sqrt(v[0]*v[0]+v[1]*v[1]+1));}else p[0]=2.f;}
            odd.depth_values=depth;odd.create();check(pass.prepare_targets(31,17),"odd targets");require(hx.settle(A.cam),"odd settle");
            // The repair-pixel census around this transaction: every odd (geometry) column has only sky half samples, so
            // 15 x 17 = 255 of the 527 pixels need the repair march and the even (sky) columns are all served; the repair's
            // clip also drops a march that comes out exactly empty (T 1, S 0), so the count is the needed pixels whose CPU
            // march is not exactly empty.
            Com<IDirect3DQuery9> occlusion;check(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&occlusion.p),"census query");
            CensusMarks marks;marks.query=occlusion.p;pass.configure_sync_timing(&marks);
            FogResult r;const HRESULT hr=hx.execute(A,odd,r);pass.configure_sync_timing(nullptr);
            DWORD counted=0;HRESULT census_got=S_FALSE;bool census_pairs=false;
            {const LONGLONG start=ticks();
             while((census_got=occlusion->GetData(&counted,sizeof counted,D3DGETDATA_FLUSH))==S_FALSE&&seconds(start)<5)Sleep(0);
             census_pairs=marks.begins[x3m::gpu_sync_timing::FogMarch]==1&&marks.ends[x3m::gpu_sync_timing::FogMarch]==1&&marks.begins[x3m::gpu_sync_timing::FogComposite]==1&&
                              marks.ends[x3m::gpu_sync_timing::FogComposite]==1&&marks.begins[x3m::gpu_sync_timing::FogRepair]==1&&marks.ends[x3m::gpu_sync_timing::FogRepair]==1;}
            const auto out=half_image(surface_bytes(d,odd.target_surface.p));const auto lit=half_image(surface_bytes(d,pass.fixture_st()));
            require(hr==S_OK&&r.applied&&r.half_width==16&&r.half_height==9,"odd_31x17_transaction_half_16x9");
            const FogFrame f=make_frame(A,odd,hx.frame,true);const fog_cpu::Setup cpu=cpu_setup(f,config);const double scene_rgb[3]={.25,.5,.75};
            double repair_worst=0,shifted_worst=0,composite_worst=0;bool alpha=true;unsigned repaired=0;
            for(UINT y=0;y<17;++y)for(UINT x=0;x<31;++x){
                const float* g=&out[(std::size_t(y)*31+x)*4];alpha=alpha&&g[3]==half_to_float(odd.colour[3]);
                if(x%2){ // no sky-class half sample can serve a geometry pixel: repaired at full resolution
                    if(y%3)continue;double v[3];raster_ray(f,x,y,v);const double depth_b=double(depth[(std::size_t(y)*31+x)*4+2]);const auto ref=fog_cpu::march(cpu,v,depth_b);
                    raster_ray(f,x+.5,y+.5,v);const auto shifted=fog_cpu::march(cpu,v,depth_b);++repaired;
                    for(int c=0;c<3;++c){const double k=double(cpu_look_rows[8][c]); // the look's per-channel extinction exponent
                        repair_worst=std::max(repair_worst,std::fabs(fog_cpu::apply(scene_rgb[c],ref.S[c],ref.T,1,k)-g[c]));shifted_worst=std::max(shifted_worst,std::fabs(fog_cpu::apply(scene_rgb[c],shifted.S[c],shifted.T,1,k)-g[c]));}
                }else{ // sky: the composite's 2x2 footprint over the half-resolution (S,T), clamped at the odd edge (half 15 / 8)
                    const UINT hx0=x/2,hy0=y/2,hy1=std::min(hy0+1,8u);const double fy=(y%2)?.5:0;
                    for(int c=0;c<3;++c){const double S=lit[(std::size_t(hy0)*16+hx0)*4+c]*(1-fy)+lit[(std::size_t(hy1)*16+hx0)*4+c]*fy,T=lit[(std::size_t(hy0)*16+hx0)*4+3]*(1-fy)+lit[(std::size_t(hy1)*16+hx0)*4+3]*fy;
                        composite_worst=std::max(composite_worst,std::fabs(fog_cpu::apply(scene_rgb[c],S,T,1,double(cpu_look_rows[8][c]))-g[c]));}
                }
            }
            std::printf("REPAIR odd_target=31x17 repaired_checked=%u worst_vs_cpu=%.6f half_pixel_shift_control=%.6f composite_worst=%.6f\n",repaired,repair_worst,shifted_worst,composite_worst);
            unsigned needed=0,written=0;
            for(UINT y=0;y<17;++y)for(UINT x=1;x<31;x+=2){
                double v[3];raster_ray(f,x,y,v);const auto m=fog_cpu::march(cpu,v,double(depth[(std::size_t(y)*31+x)*4+2]));++needed;
                written+=!(m.T==1.0&&m.S[0]==0.0&&m.S[1]==0.0&&m.S[2]==0.0);
            }
            std::printf("REPAIR_CENSUS odd_target=31x17 counted=%lu needed=%u written_cpu=%u area=%u census_brackets=%u/%u pairs=%u get=%08lx issue=%08lx\n",(unsigned long)counted,needed,written,
                        unsigned(marks.area),marks.census_begins,marks.census_ends,unsigned(census_pairs),(unsigned long)census_got,(unsigned long)marks.issue);
            require(census_got==S_OK&&SUCCEEDED(marks.issue)&&needed==255&&written>0&&written<needed&&counted==written&&marks.area==527&&marks.census_begins==1&&marks.census_ends==1&&census_pairs,
                    "repair_census_counts_the_pixels_the_repair_writes");
            require(repair_worst<=1e-3&&alpha,"repair_matches_cpu_reference_through_its_own_raster_pixel");
            require(shifted_worst>2e-3&&shifted_worst>3*repair_worst,"repair_reference_rejects_a_half_pixel_offset");
            require(composite_worst<=1e-3,"composite_footprint_clamps_at_odd_half_target_edge");
            odd.release();
        }
        // --- Shutdown joins the worker promptly, also in the middle of a fill ---
        {
            pass.invalidate_density();for(int i=0;i<3;++i){pass.prepare_density(config,A.cam,++hx.frame);Sleep(1);}
            scene.release();const LONGLONG start=ticks();pass.detach();const double ms=seconds(start)*1e3;std::printf("DETACH mid_fill_join_ms=%.2f\n",ms);
            // The device release path (MotionOutput::release_resources -> FogPass::detach) with a live, generating worker.
            require(ms<500&&!pass.fixture_density_cache()&&pass.references()==0,"detach_mid_fill_joins_worker_and_releases_everything");
            const unsigned references_after=device_references();std::printf("DEVICE_REFERENCES before_attach=%u after_detach=%u\n",references_before,references_after);
            require(references_after==references_before,"device_release_path_leaves_device_refcount_balanced");
            require(pass.prepare_density(config,A.cam,++hx.frame)==E_INVALIDARG&&!pass.fixture_density_cache(),"detached_pass_starts_no_worker");
        }
        std::printf("PREPARE_CPU first_prepare_ms=%.2f upload_frames=%u median_us=%.1f p95_us=%.1f p99_us=%.1f max_us=%.1f us_per_update_surface=%.2f idle_frames=%u idle_median_us=%.2f idle_p95_us=%.2f idle_max_us=%.1f max_upload_bytes=%u max_update_surface_calls=%u budget_bytes=%u budget_rects=%u\n",hx.first_prepare_us/1e3,unsigned(hx.prepare_upload_us.size()),percentile(hx.prepare_upload_us,.5),percentile(hx.prepare_upload_us,.95),percentile(hx.prepare_upload_us,.99),percentile(hx.prepare_upload_us,1),
            hx.upload_calls?hx.upload_us_total/hx.upload_calls:0.,unsigned(hx.prepare_idle_us.size()),percentile(hx.prepare_idle_us,.5),percentile(hx.prepare_idle_us,.95),percentile(hx.prepare_idle_us,1),hx.max_upload_bytes,hx.max_upload_rects,unsigned(kDefaultUploadBudget),kDefaultUploadRects);
        std::printf("STATIC_GENERATION nodes=%llu seconds=%.3f nodes_per_second=%.0f\n",(unsigned long long)hx.static_nodes,hx.static_seconds,hx.static_seconds?hx.static_nodes/hx.static_seconds:0.);
    }
    // --- The visibility grid refused (an injected RGBA8 target failure): the stored fog keeps drawing with the in-march
    // programs, byte-identical to the first instance's split frame, and says why once ---
    {
        FogDensityConfig gconfig=config;gconfig.shadow_pass=true;refuse_rgba8_targets=true;
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"refused grid attach");Harness hx(d,caps,pass,gconfig);
        Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"refused grid targets");
        require(hx.settle(A.cam)&&!pass.fixture_grid()&&pass.density_status().available,"refused_grid_keeps_the_stored_path_available");
        const char* reason=pass.density_status().shadow_pass_refused;
        require(reason&&std::string(reason)=="density_grid_target","refused_grid_reports_density_grid_target");
        const UINT N=64;std::vector<float> split_map(N*N);for(UINT y=0;y<N;++y)for(UINT x=0;x<N;++x)split_map[y*N+x]=x<N/2?1.f:0.f;
        Com<IDirect3DTexture9> split_texture;upload(d,N,N,D3DFMT_R32F,4,split_map.data(),0,&split_texture.p);
        check(hx.prepare(A.cam),"refused grid prepare");FogFrame f=make_frame(A,scene,hx.frame,true);
        f.count=1;auto& k=f.cascades[0];k.map=split_texture.p;k.valid=true;k.frame=hx.frame;k.bias=0;k.rows[0]=1e-5f;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;k.texel_world=36.6f;k.depth_range=200000.f;
        FogResult r;const HRESULT hr=hx.execute(A,scene,r,false,&f);
        require(hr==S_OK&&r.applied&&!r.grid&&r.cascades_bound==1&&surface_bytes(d,pass.fixture_st())==off_split,"refused_grid_draws_the_in_march_split_frame_byte_identical");
        refuse_rgba8_targets=false;check(hx.prepare(A.cam),"prepare after the injection ends");
        require(!pass.fixture_grid()&&pass.density_status().shadow_pass_refused==reason,"refused_grid_stays_refused_until_detach");
        split_texture.reset();scene.release();pass.detach();require(pass.references()==0,"refused_grid_detach_releases_everything");
    }
    // --- The visibility grid (FogDensityConfig::shadow_pass, docs/architecture/fog-shadow-pass.md): programs and the
    // RGBA8 target at prepare, one quad before the march under hostile caller state, Reset, detach ---
    {
        FogDensityConfig gconfig=config;gconfig.shadow_pass=true;
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"grid attach");Harness hx(d,caps,pass,gconfig);
        Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"grid targets");
        require(hx.settle(A.cam)&&pass.fixture_grid()!=nullptr,"grid_pose_settles_with_the_grid_target");
        const UINT N=64;std::vector<float> lit_map(N*N,1.f),split_map(N*N);for(UINT y=0;y<N;++y)for(UINT x=0;x<N;++x)split_map[y*N+x]=x<N/2?1.f:0.f;
        Com<IDirect3DTexture9> lit_texture,split_texture;upload(d,N,N,D3DFMT_R32F,4,lit_map.data(),0,&lit_texture.p);upload(d,N,N,D3DFMT_R32F,4,split_map.data(),0,&split_texture.p);
        auto shade=[&](IDirect3DTexture9* map,float x_scale,FogResult& r){
            check(hx.prepare(A.cam),"grid prepare");FogFrame f=make_frame(A,scene,hx.frame,true);
            if(map){f.count=1;auto& k=f.cascades[0];k.map=map;k.valid=true;k.frame=hx.frame;k.bias=0;k.rows[0]=x_scale;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;k.texel_world=36.6f;k.depth_range=200000.f;}
            if(hx.execute(A,scene,r,false,&f)!=S_OK||!r.applied||r.cascades_bound!=(map?1u:0u))throw std::runtime_error("grid transaction");return surface_bytes(d,pass.fixture_st());
        };
        FogResult r_none,r_lit,r_split;const auto none=shade(nullptr,0,r_none),all_lit=shade(lit_texture.p,1e-9f,r_lit);const auto split_bytes=shade(split_texture.p,1e-5f,r_split);
        require(!r_none.grid&&r_lit.grid&&r_split.grid,"grid_pass_draws_only_with_a_cascade");
        require(none==off_none,"grid_no_cascade_frame_identical_to_in_march_programs");
        require(all_lit==none,"grid_fully_lit_map_is_bit_identical_to_no_map");
        // Against the in-march programs' split frame: transmittance bit-identical (visibility touches sun light only),
        // in-scatter moved where the fog is shadowed.
        const auto split=half_image(split_bytes),off=half_image(off_split);double worst=0;bool t_same=true;unsigned differs=0,fogged=0;
        for(std::size_t i=0;i<split.size();i+=4){t_same=t_same&&split[i+3]==off[i+3];if(!(split[i+3]<1.f))continue;++fogged;for(int c=0;c<3;++c)worst=std::max(worst,std::fabs(double(split[i+c])-off[i+c]));differs+=split[i+1]!=off[i+1];}
        std::printf("GRID extra_device_calls=%d fogged_pixels=%u in_scatter_differs=%u worst_S_vs_in_march=%.6f\n",int(r_split.device_calls)-int(r_none.device_calls),fogged,differs,worst);
        require(t_same&&fogged>0&&differs>0,"grid_transmittance_identical_in_scatter_moves");
        // The A/B toggle (fog-shadow-pass.md, "A/B toggle and log row"): the per-frame report of the split frame, then
        // prepare_density with shadow_pass off (what Ctrl+Shift+F11 hands over) draws the in-march programs' split frame
        // byte-identically with the grid kept allocated and nothing created; back on, the grid frame returns unchanged.
        // One cascade: the gross count is visibility 16 + march 3 + repair 1 = 20, the net 16 - 1 (the march's own
        // constant upload) + (3 - 3) + (1 - 1) = 15 (with two or more cascades the in-march repair binds two: 14).
        {
            const FogGridReport g=pass.grid_report();
            std::printf("GRID_REPORT frame_current=%u drawn=%u bind=%08lx unshadowed=%s cascades=%u kernel=%.4g,%.4g,%.4g far_width=%.1f frame_term=%.4f calls=%u net_calls=%d\n",
                        unsigned(g.frame==hx.frame),unsigned(g.drawn),(unsigned long)g.bind,g.unshadowed,g.cascades,g.kernel[0],g.kernel[1],g.kernel[2],g.far_width,g.frame_term,g.calls,g.net_calls);
            require(g.frame==hx.frame&&g.drawn&&g.bind==S_OK&&std::string(g.unshadowed)=="none"&&g.cascades==1u&&g.kernel[0]>0&&g.far_width>0&&g.calls==20u&&g.net_calls==15,"grid_report_counts_the_pass_calls");
            const unsigned toggle_references=pass.references(),toggle_allocations=pass.allocations();IDirect3DTexture9* const kept_grid=pass.fixture_grid();
            hx.config.shadow_pass=false;FogResult r_off;const auto toggled_off=shade(split_texture.p,1e-5f,r_off);
            std::printf("GRID_TOGGLE off_calls=%u on_calls=%u difference=%d\n",r_off.device_calls,r_split.device_calls,int(r_split.device_calls)-int(r_off.device_calls));
            require(toggled_off==off_split&&!r_off.grid&&!pass.grid_variant()&&pass.grid_report().frame!=hx.frame,"grid_toggled_off_draws_the_in_march_split_frame_byte_identical");
            require(pass.fixture_grid()==kept_grid&&kept_grid&&pass.references()==toggle_references&&pass.allocations()==toggle_allocations,"grid_toggled_off_keeps_the_grid_and_creates_nothing");
            require(int(r_split.device_calls)-int(r_off.device_calls)==g.net_calls,"grid_report_net_calls_match_the_toggled_off_frame");
            hx.config.shadow_pass=true;FogResult r_on;const auto toggled_on=shade(split_texture.p,1e-5f,r_on);
            require(toggled_on==split_bytes&&r_on.grid&&r_on.device_calls==r_split.device_calls&&pass.fixture_grid()==kept_grid&&pass.allocations()==toggle_allocations,"grid_toggled_back_on_byte_identical");
        }
        const unsigned references=pass.references(),allocations=pass.allocations();
        FogResult again;const auto repeat=shade(split_texture.p,1e-5f,again);
        require(repeat==split_bytes&&pass.references()==references&&pass.allocations()==allocations,"grid_frames_byte_identical_create_and_allocate_nothing");
        // Reset: the grid target goes with the other targets and returns at the next prepare; the frame is byte-identical.
        // (The fixture's own DEFAULT maps are released first: a live DEFAULT resource makes Reset refuse.)
        lit_texture.reset();split_texture.reset();
        scene.release();pass.before_reset();const HRESULT reset=d->Reset(&pp);pass.after_reset(reset);check(reset,"grid Reset");scene.create();
        require(!pass.fixture_grid()&&!pass.reset_pending(),"grid_reset_released_the_grid_target");
        check(pass.prepare_targets(scene.w,scene.h),"grid targets after Reset");require(hx.settle(A.cam)&&pass.fixture_grid()!=nullptr,"grid_reset_recreates_the_target_at_prepare");
        upload(d,N,N,D3DFMT_R32F,4,split_map.data(),0,&split_texture.p);
        FogResult after;require(shade(split_texture.p,1e-5f,after)==split_bytes&&after.grid,"grid_after_reset_byte_identical_to_before");
        split_texture.reset();
        scene.release();pass.detach();require(pass.references()==0&&!pass.fixture_grid(),"grid_detach_releases_everything");
        require(device_references()==references_before,"grid_device_refcount_balanced");
    }
    // --- Far bins 24 (FogDensityConfig::far_bins, fog-gpu-cost.md step B): the variant's march/repair created at prepare,
    // its frame against the CPU twin at 24 far bins, the same pass back at 40 drawing the default frames byte for byte,
    // any other count refused at prepare, Reset (programs kept) and detach ---
    {
        FogDensityConfig fconfig=config;fconfig.far_bins=x3m::renderer::fog_far_bins_coarse;
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"far24 attach");Harness hx(d,caps,pass,fconfig);
        Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"far24 targets");
        require(hx.settle(A.cam)&&pass.density_far_bins()==24u,"far24_pose_settles_with_the_far24_programs");
        const UINT N=64;std::vector<float> split_map(N*N);for(UINT y=0;y<N;++y)for(UINT x=0;x<N;++x)split_map[y*N+x]=x<N/2?1.f:0.f;
        Com<IDirect3DTexture9> split_texture;upload(d,N,N,D3DFMT_R32F,4,split_map.data(),0,&split_texture.p);
        auto shade=[&](IDirect3DTexture9* map,FogResult& r){ // the shaft block's frames: no map, or the split map at x = 1e-5 view x
            check(hx.prepare(A.cam),"far24 prepare");FogFrame f=make_frame(A,scene,hx.frame,true);
            if(map){f.count=1;auto& k=f.cascades[0];k.map=map;k.valid=true;k.frame=hx.frame;k.bias=0;k.rows[0]=1e-5f;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;}
            if(hx.execute(A,scene,r,false,&f)!=S_OK||!r.applied||r.cascades_bound!=(map?1u:0u))throw std::runtime_error("far24 transaction");return surface_bytes(d,pass.fixture_st());
        };
        const unsigned references=pass.references(),allocations=pass.allocations();
        FogResult r24,r24s;const auto none24=shade(nullptr,r24);const FogFrame f=make_frame(A,scene,hx.frame,true);const auto split24=shade(split_texture.p,r24s);
        {
            const auto lit=half_image(none24);fog_cpu::Setup cpu=cpu_setup(f,fconfig),cpu40=cpu;cpu40.far_bins=x3m::renderer::fog_far_bins_default;
            double worst_T=0,worst_S=0,control_T=0,control_S=0;
            for(UINT y=1;y<36;y+=7)for(UINT x=2;x<64;x+=9){double view[3];raster_ray(f,2.0*x,2.0*y,view);const auto ref=fog_cpu::march(cpu,view,0),ref40=fog_cpu::march(cpu40,view,0);const float* g=&lit[(std::size_t(y)*64+x)*4];
                worst_T=std::max(worst_T,std::fabs(ref.T-g[3]));control_T=std::max(control_T,std::fabs(ref40.T-g[3]));
                for(int c=0;c<3;++c){worst_S=std::max(worst_S,std::fabs(ref.S[c]-g[c]));control_S=std::max(control_S,std::fabs(ref40.S[c]-g[c]));}}
            // Transmittance identical with the split map (visibility touches sun light only), in-scatter moved: the shaft law holds.
            const auto split=half_image(split24);bool t_same=true;unsigned differs=0;for(std::size_t i=0;i<split.size();i+=4){t_same=t_same&&split[i+3]==lit[i+3];differs+=split[i+1]!=lit[i+1];}
            std::printf("FAR24 sky_pixels=35 worst_T=%.6f worst_S=%.6f vs_40_bin_twin_T=%.6f vs_40_bin_twin_S=%.6f split_differs=%u device_calls=%u\n",worst_T,worst_S,control_T,control_S,differs,r24.device_calls);
            require(worst_T<=.003&&worst_S<=.003,"far24_march_matches_cpu_reference_at_24_far_bins");
            require(none24!=off_none&&split24!=off_split,"far24_frames_differ_from_the_40_bin_frames");
            require(t_same&&differs>0,"far24_shafts_keep_transmittance_and_move_in_scatter");
        }
        // The same pass asked for 40: the default programs replace the pair at prepare and draw the first instance's frames.
        const unsigned creates_before_swap=pixel_shader_creates;
        hx.config.far_bins=x3m::renderer::fog_far_bins_default;FogResult r40,r40s;const auto none40=shade(nullptr,r40);const bool swapped=pass.density_far_bins()==40u;const auto split40=shade(split_texture.p,r40s);
        require(pixel_shader_creates-creates_before_swap==2u,"far_bins_swap_creates_only_the_march_repair_pair");
        require(swapped&&none40==off_none&&split40==off_split,"far_bins_40_on_the_same_pass_draws_the_default_frames_byte_identical");
        require(r40.device_calls==r24.device_calls&&r40s.device_calls==r24s.device_calls,"far_bins_variants_issue_the_same_device_calls");
        require(pass.references()==references&&pass.allocations()==allocations,"far_bins_swap_recreates_programs_only");
        hx.config.far_bins=x3m::renderer::fog_far_bins_coarse;FogResult again;
        require(shade(nullptr,again)==none24&&pass.density_far_bins()==24u,"far_bins_24_again_byte_identical");
        hx.config.far_bins=32;const HRESULT refused=hx.prepare(A.cam);
        require(refused==E_INVALIDARG&&pass.density_far_bins()==24u&&!pass.density_status().available&&pass.references()==references,"far_bins_other_than_40_or_24_refused_at_prepare");
        hx.config.far_bins=x3m::renderer::fog_far_bins_coarse;
        // Reset: pixel shaders survive it; the frame after the re-upload is byte-identical.
        split_texture.reset();scene.release();pass.before_reset();const HRESULT reset=d->Reset(&pp);pass.after_reset(reset);check(reset,"far24 Reset");scene.create();
        check(pass.prepare_targets(scene.w,scene.h),"far24 targets after Reset");require(hx.settle(A.cam),"far24_settles_after_reset");
        FogResult after;require(shade(nullptr,after)==none24&&pass.density_far_bins()==24u,"far24_after_reset_byte_identical_programs_kept");
        // The cap rule: above fog_far_bins_coarse_cap_max (ds would reach 1.9 far nodes at 200,000) 40 draws, said as "cap";
        // back at the accepted cap the 24-bin pair draws again (not sticky: the cap is a launch value).
        {
            const float cap=hx.config.look.sky_cap;hx.config.look.sky_cap=150000.f;const HRESULT above=hx.prepare(A.cam);
            const char* why=pass.density_status().far_bins_refused;const bool forty=pass.density_far_bins()==40u;
            hx.config.look.sky_cap=cap;const HRESULT below=hx.prepare(A.cam);
            require(above==S_OK&&forty&&why&&std::string(why)=="cap"&&below==S_OK&&pass.density_far_bins()==24u&&!pass.density_status().far_bins_refused,"far24_refused_above_the_cap_and_back_below_it");
        }
        // A 24-bin pair that cannot be built (injected CreatePixelShader failure) while 40 draws: the working pair stays,
        // the shared composite is not re-created, "program" is reported, and 24 is not tried again until detach.
        {
            hx.config.far_bins=x3m::renderer::fog_far_bins_default;check(hx.prepare(A.cam),"far24 back to 40");
            const unsigned held_references=pass.references();refuse_far24_programs=true;hx.config.far_bins=x3m::renderer::fog_far_bins_coarse;
            FogResult r_fail;const auto failed=shade(nullptr,r_fail);refuse_far24_programs=false;const char* why=pass.density_status().far_bins_refused;
            const unsigned creates=pixel_shader_creates;FogResult r_again;const auto again_bytes=shade(nullptr,r_again);
            require(failed==off_none&&pass.density_far_bins()==40u&&why&&std::string(why)=="program"&&pass.references()==held_references,"far24_program_failure_keeps_the_working_pair");
            require(again_bytes==off_none&&pixel_shader_creates==creates&&pass.density_status().far_bins_refused&&pass.density_status().available,"far24_program_failure_is_sticky_without_retries");
        }
        // The shadow pass: 24 is clamped to 40 as soon as a prepare asks for the grid, and stays so with the grid toggled
        // off, so the grid frame and its toggled-off frame draw one far law (the in-march default frame).
        {
            FogPass shadow;check(shadow.attach(d,table,caps,D3DFMT_X8R8G8B8),"far24 shadow attach");FogDensityConfig sconfig=fconfig;sconfig.shadow_pass=true;
            Harness hs(d,caps,shadow,sconfig);Scene s2(d,caps,128,72);s2.create();check(shadow.prepare_targets(s2.w,s2.h),"far24 shadow targets");
            const bool settled=hs.settle(A.cam);const char* why=shadow.density_status().far_bins_refused;const bool forty=shadow.density_far_bins()==40u;
            hs.config.shadow_pass=false;check(hs.prepare(A.cam),"far24 shadow toggled off");FogResult r_off;
            const bool toggled=hs.execute(A,s2,r_off)==S_OK&&r_off.applied&&surface_bytes(d,shadow.fixture_st())==off_none;
            const char* still=shadow.density_status().far_bins_refused;
            require(settled&&forty&&why&&std::string(why)=="shadow_pass"&&toggled&&still&&std::string(still)=="shadow_pass"&&shadow.density_far_bins()==40u,"far24_clamped_to_40_with_the_shadow_pass_and_toggled_off");
            s2.release();shadow.detach();require(shadow.references()==0,"far24_shadow_detach_releases_everything");
        }
        scene.release();pass.detach();require(pass.references()==0&&pass.density_far_bins()==0u,"far24_detach_releases_everything");
        require(device_references()==references_before,"far24_device_refcount_balanced");
    }
    // --- Quarter-resolution march (FogDensityConfig::march_scale 4, fog-gpu-cost.md step C): the quarter programs and target
    // created at prepare, the march against the CPU twin at the quarter rays, the same pass back at 2 drawing the default
    // frames byte for byte, the 24-far-bin combination, any other spacing refused, Reset, an injected program failure, detach ---
    {
        FogDensityConfig qconfig=config;qconfig.march_scale=x3m::renderer::fog_march_scale_quarter;
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"q4 attach");Harness hx(d,caps,pass,qconfig);
        Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"q4 targets");
        require(hx.settle(A.cam)&&pass.density_march_scale()==4u&&pass.density_far_bins()==40u&&!pass.density_status().march_scale_refused,"q4_pose_settles_with_the_quarter_programs");
        const UINT N=64;std::vector<float> split_map(N*N);for(UINT y=0;y<N;++y)for(UINT x=0;x<N;++x)split_map[y*N+x]=x<N/2?1.f:0.f;
        Com<IDirect3DTexture9> split_texture;upload(d,N,N,D3DFMT_R32F,4,split_map.data(),0,&split_texture.p);
        std::vector<std::uint8_t> scene_bytes;
        auto shade=[&](IDirect3DTexture9* map,FogResult& r){ // the shaft block's frames: no map, or the split map at x = 1e-5 view x
            check(hx.prepare(A.cam),"q4 prepare");FogFrame f=make_frame(A,scene,hx.frame,true);
            if(map){f.count=1;auto& k=f.cascades[0];k.map=map;k.valid=true;k.frame=hx.frame;k.bias=0;k.rows[0]=1e-5f;k.rows[5]=1e-9f;k.rows[10]=1e-9f;k.rows[11]=.5f;}
            if(hx.execute(A,scene,r,false,&f)!=S_OK||!r.applied||r.cascades_bound!=(map?1u:0u))throw std::runtime_error("q4 transaction");
            scene_bytes=surface_bytes(d,scene.target_surface.p);return surface_bytes(d,pass.fixture_st());
        };
        // The CPU twin at the quarter rays (full pixel 4p) of target pixels (x, y), against a control law.
        auto twin=[&](const std::vector<std::uint8_t>& bytes,const FogFrame& f,const fog_cpu::Setup& cpu,const fog_cpu::Setup& control_cpu,bool control_half_rays,double& worst_T,double& worst_S,double& control){
            const auto lit=half_image(bytes);worst_T=worst_S=control=0;
            for(UINT y=1;y<18;y+=4)for(UINT x=1;x<32;x+=5){
                double view[3],other_view[3];raster_ray(f,4.0*x,4.0*y,view);raster_ray(f,(control_half_rays?2.0:4.0)*x,(control_half_rays?2.0:4.0)*y,other_view);
                const auto ref=fog_cpu::march(cpu,view,0),other=fog_cpu::march(control_cpu,other_view,0);const float* g=&lit[(std::size_t(y)*32+x)*4];
                worst_T=std::max(worst_T,std::fabs(ref.T-g[3]));control=std::max(control,std::fabs(other.T-g[3]));
                for(int c=0;c<3;++c){worst_S=std::max(worst_S,std::fabs(ref.S[c]-g[c]));control=std::max(control,std::fabs(other.S[c]-g[c]));}
            }
        };
        const unsigned references=pass.references(),allocations=pass.allocations();
        FogResult r4,r4s;const auto none4=shade(nullptr,r4);const auto scene4=scene_bytes;const FogFrame f=make_frame(A,scene,hx.frame,true);const auto split4=shade(split_texture.p,r4s);
        require(r4.half_width==32&&r4.half_height==18&&none4.size()==std::size_t(32)*18*8&&r4.lit!=nullptr,"q4_march_target_is_quarter_32x18");
        {
            const fog_cpu::Setup cpu=cpu_setup(f,qconfig);double worst_T=0,worst_S=0,control=0;twin(none4,f,cpu,cpu,true,worst_T,worst_S,control);
            const auto lit=half_image(none4),split=half_image(split4);bool t_same=true;unsigned differs=0;
            for(std::size_t i=0;i<split.size();i+=4){t_same=t_same&&split[i+3]==lit[i+3];differs+=split[i+1]!=lit[i+1];}
            std::printf("Q4 sky_pixels=35 worst_T=%.6f worst_S=%.6f half_ray_law_control=%.6f split_differs=%u device_calls=%u\n",worst_T,worst_S,control,differs,r4.device_calls);
            require(worst_T<=.003&&worst_S<=.003&&control>2*std::max(worst_T,worst_S),"q4_march_matches_cpu_reference_at_the_quarter_rays");
            require(t_same&&differs>0,"q4_shafts_keep_transmittance_and_move_in_scatter");
        }
        // The same pass asked for 2: the default march, repair and composite replace the set at prepare, the quarter target
        // goes, and the first instance's frames are drawn byte for byte with the same device calls.
        const unsigned creates_before_swap=pixel_shader_creates;
        hx.config.march_scale=x3m::renderer::fog_march_scale_default;FogResult r2,r2s;const auto none2=shade(nullptr,r2);const auto scene2=scene_bytes;
        const bool swapped=pass.density_march_scale()==2u;const auto split2=shade(split_texture.p,r2s);
        require(pixel_shader_creates-creates_before_swap==3u,"q4_swap_creates_the_march_repair_composite_set");
        require(swapped&&none2==off_none&&split2==off_split,"march_scale_2_on_the_same_pass_draws_the_default_frames_byte_identical");
        require(r2.device_calls==r4.device_calls&&r2s.device_calls==r4s.device_calls,"march_scale_variants_issue_the_same_device_calls");
        require(pass.references()+2==references&&pass.allocations()==allocations,"march_scale_2_releases_the_quarter_target");
        require(scene4!=scene2,"q4_scene_differs_from_the_half_resolution_scene");
        hx.config.march_scale=x3m::renderer::fog_march_scale_quarter;FogResult again;
        require(shade(nullptr,again)==none4&&pass.density_march_scale()==4u&&pass.references()==references&&pass.allocations()==allocations+1,"q4_again_byte_identical_target_recreated");
        hx.config.march_scale=3;const HRESULT refused=hx.prepare(A.cam);
        require(refused==E_INVALIDARG&&pass.density_march_scale()==4u&&!pass.density_status().available&&pass.references()==references,"march_scale_other_than_2_or_4_refused_at_prepare");
        hx.config.march_scale=x3m::renderer::fog_march_scale_quarter;
        // Both far-bin counts combine with the quarter spacing: 24 swaps the march/repair pair only (the composite is per spacing).
        {
            const unsigned creates=pixel_shader_creates;hx.config.far_bins=x3m::renderer::fog_far_bins_coarse;FogResult r24;const auto none24=shade(nullptr,r24);
            const bool pair_only=pixel_shader_creates-creates==2u;const FogFrame f24=make_frame(A,scene,hx.frame,true);
            fog_cpu::Setup cpu=cpu_setup(f24,hx.config),cpu40=cpu;cpu40.far_bins=x3m::renderer::fog_far_bins_default;double worst_T=0,worst_S=0,control=0;
            twin(none24,f24,cpu,cpu40,false,worst_T,worst_S,control);
            std::printf("Q4_FAR24 sky_pixels=35 worst_T=%.6f worst_S=%.6f vs_40_bin_twin=%.6f\n",worst_T,worst_S,control);
            require(pair_only&&pass.density_far_bins()==24u&&pass.density_march_scale()==4u&&none24!=none4&&worst_T<=.003&&worst_S<=.003&&control>worst_S,"q4_far24_combination_matches_cpu_reference_at_24_far_bins");
            hx.config.far_bins=x3m::renderer::fog_far_bins_default;FogResult r40;
            require(shade(nullptr,r40)==none4&&pass.density_far_bins()==40u&&pass.density_march_scale()==4u,"q4_far_bins_back_to_40_byte_identical");
        }
        // Reset: the quarter target goes with the others and comes back at the next prepare_density; programs are kept.
        split_texture.reset();scene.release();pass.before_reset();const bool released=pass.fixture_st()==nullptr;const HRESULT reset=d->Reset(&pp);pass.after_reset(reset);check(reset,"q4 Reset");scene.create();
        check(pass.prepare_targets(scene.w,scene.h),"q4 targets after Reset");require(hx.settle(A.cam),"q4_settles_after_reset");
        {FogResult after;require(released&&shade(nullptr,after)==none4&&pass.density_march_scale()==4u&&after.half_width==32u,"q4_after_reset_byte_identical_target_recreated");}
        // A quarter set that cannot be built (injected CreatePixelShader failure) while 2 draws: the working set stays, "program"
        // is reported, and 4 is not tried again until detach.
        {
            hx.config.march_scale=x3m::renderer::fog_march_scale_default;check(hx.prepare(A.cam),"q4 back to 2");
            const unsigned held_references=pass.references();refuse_q4_programs=true;hx.config.march_scale=x3m::renderer::fog_march_scale_quarter;
            FogResult r_fail;const auto failed=shade(nullptr,r_fail);refuse_q4_programs=false;const char* why=pass.density_status().march_scale_refused;
            const unsigned creates=pixel_shader_creates;FogResult r_again;const auto again_bytes=shade(nullptr,r_again);
            require(failed==off_none&&pass.density_march_scale()==2u&&why&&std::string(why)=="program"&&pass.references()==held_references,"q4_program_failure_keeps_the_working_set");
            require(again_bytes==off_none&&pixel_shader_creates==creates&&pass.density_status().march_scale_refused&&pass.density_status().available,"q4_program_failure_is_sticky_without_retries");
        }
        scene.release();pass.detach();require(pass.references()==0&&pass.density_march_scale()==0u,"q4_detach_releases_everything");
        require(device_references()==references_before,"q4_device_refcount_balanced");
    }
    // --- Double failure after a Reset while 4 draws (review F1): the quarter target cannot be re-created and the half set cannot
    // be built in that prepare. The quarter set is dropped (it cannot draw without its target), 4 stays refused as "target",
    // and the next prepare builds the half set from scratch and draws the default frame ---
    {
        FogDensityConfig qconfig=config;qconfig.march_scale=x3m::renderer::fog_march_scale_quarter;
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"q4 double attach");Harness hx(d,caps,pass,qconfig);
        Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"q4 double targets");
        const bool at_four=hx.settle(A.cam)&&pass.density_march_scale()==4u;
        scene.release();pass.before_reset();const HRESULT reset=d->Reset(&pp);pass.after_reset(reset);check(reset,"q4 double Reset");scene.create();
        check(pass.prepare_targets(scene.w,scene.h),"q4 double targets after Reset");
        refuse_fp16_target_width=32;refuse_default_march=true;const HRESULT hr=hx.prepare(A.cam);refuse_fp16_target_width=0;refuse_default_march=false;
        const char* why=pass.density_status().march_scale_refused;
        const bool dropped=!pass.density_ready(scene.w,scene.h)&&pass.density_march_scale()==2u&&why&&std::string(why)=="target";
        const bool settled=hx.settle(A.cam);FogResult r;const HRESULT drawn=hx.execute(A,scene,r);const auto bytes=surface_bytes(d,pass.fixture_st());
        const char* still=pass.density_status().march_scale_refused;
        std::printf("Q4_DOUBLE prepare=%08lx dropped=%u settled=%u drawn=%08lx half=%ux%u reason=%s\n",(unsigned long)hr,unsigned(dropped),unsigned(settled),(unsigned long)drawn,r.half_width,r.half_height,still?still:"none");
        require(at_four&&hr!=D3DERR_DEVICELOST&&!pass.reset_pending()&&dropped,"q4_double_failure_after_reset_drops_the_quarter_set_and_keeps_target");
        require(settled&&drawn==S_OK&&r.applied&&r.half_width==64u&&bytes==off_none&&pass.density_march_scale()==2u&&still&&std::string(still)=="target",
                "q4_double_failure_next_prepare_builds_the_half_set_and_4_stays_refused");
        scene.release();pass.detach();require(pass.references()==0,"q4_double_failure_detach_releases_everything");
    }
    // --- Quarter spacing refused at the first prepare: its target cannot be created, its programs cannot be built, or the
    // shadow pass is asked (its grid programs exist at spacing 2 only, also toggled off). The half-resolution march draws the
    // default frame each time, the reason is reported and stays ---
    {
        FogDensityConfig qconfig=config;qconfig.march_scale=x3m::renderer::fog_march_scale_quarter;
        auto frame_bytes=[&](FogPass& pass,Harness& hx,Scene& scene){FogResult r;check(hx.prepare(A.cam),"q4 refused prepare");
            if(hx.execute(A,scene,r)!=S_OK||!r.applied)throw std::runtime_error("q4 refused transaction");return surface_bytes(d,pass.fixture_st());};
        for(int kind=0;kind<3;++kind){
            static const char* const expected[3]={"target","program","shadow_pass"};
            FogDensityConfig c=qconfig;c.shadow_pass=kind==2;
            if(kind==0)refuse_fp16_target_width=32;else if(kind==1)refuse_q4_programs=true;
            FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"q4 refused attach");Harness hx(d,caps,pass,c);
            Scene scene(d,caps,128,72);scene.create();check(pass.prepare_targets(scene.w,scene.h),"q4 refused targets");
            const bool settled=hx.settle(A.cam);refuse_fp16_target_width=0;refuse_q4_programs=false;
            const char* why=pass.density_status().march_scale_refused;const bool half=pass.density_march_scale()==2u;
            if(kind==2)hx.config.shadow_pass=false; // toggled off: still the half-resolution spacing
            const auto bytes=frame_bytes(pass,hx,scene);const char* still=pass.density_status().march_scale_refused;
            std::printf("Q4_REFUSED kind=%s reason=%s drawn=%u\n",expected[kind],why?why:"none",pass.density_march_scale());
            const std::string label=std::string("q4_refused_")+expected[kind]+"_draws_the_half_resolution_march_and_stays_refused";
            require(settled&&half&&why&&std::string(why)==expected[kind]&&still&&std::string(still)==expected[kind]&&pass.density_march_scale()==2u&&pass.density_status().available&&
                    bytes==off_none,label.c_str());
            scene.release();pass.detach();require(pass.references()==0,(std::string("q4_refused_")+expected[kind]+"_detach_releases_everything").c_str());
        }
        require(device_references()==references_before,"q4_refused_device_refcount_balanced");
    }
    // --- The edge repair at spacings 2 and 4 and the needs-repair census (--gpu-sync-timing, step C) on two odd 31x17 layouts:
    // A, odd columns geometry (every geometry pixel is repaired at both spacings); B, columns x mod 4 in {0,1} geometry at view
    // depth 20000 (nothing is repaired at spacing 2; at 4 every sky pixel is, its samples at columns 4q being geometry).
    // Composite and repair against the CPU twin of the footprint law through their own raster pixel; the needs census against
    // the twin's needs_repair count, the repair census against the pixels with a non-empty CPU march ---
    {
        FogPass pass;check(pass.attach(d,table,caps,D3DFMT_X8R8G8B8),"census attach");Harness hx(d,caps,pass,config);
        Com<IDirect3DQuery9> occlusion,needs_occlusion;check(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&occlusion.p),"census query");check(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&needs_occlusion.p),"needs query");
        CensusMarks marks;marks.query=occlusion.p;marks.needs_query=needs_occlusion.p;pass.configure_sync_timing(&marks); // before prepare: the census program is created there
        const double scene_rgb[3]={.25,.5,.75};bool counts=true,brackets=true,settled=true;double worst_repair=0,worst_composite=0;unsigned needs_of[2][2]{},written_of[2][2]{},counted_of[2][2]{},needs_counted_of[2][2]{};
        for(int layout=0;layout<2;++layout){
            Scene odd(d,caps,31,17);std::vector<float> depth(std::size_t(31)*17*4,0.f);const FogFrame shape=make_frame(A,odd,0,true);
            for(UINT y=0;y<17;++y)for(UINT x=0;x<31;++x){
                float* p=&depth[(std::size_t(y)*31+x)*4];const bool geometry=layout==0?x%2==1:x%4<2;
                if(geometry){double v[3];raster_ray(shape,x,y,v);p[0]=.5f;p[2]=layout==0?float(60000/std::sqrt(v[0]*v[0]+v[1]*v[1]+1)):20000.f;}else p[0]=2.f;
            }
            odd.depth_values=depth;odd.create();check(pass.prepare_targets(31,17),"census odd targets");
            for(unsigned scale:{2u,4u}){
                const int si=scale==4;hx.config.march_scale=scale;
                if(!hx.settle(A.cam)||pass.density_march_scale()!=scale){settled=false;continue;}
                marks.census_begins=marks.census_ends=marks.needs_begins=marks.needs_ends=0;marks.area=marks.needs_area=marks.needs_scale=0;
                for(unsigned i=0;i<32;++i)marks.begins[i]=marks.ends[i]=0;
                marks.wanted=true;FogResult r;const HRESULT hr=hx.execute(A,odd,r);marks.wanted=false;
                DWORD counted=0,needs_counted=0;HRESULT got=S_FALSE,needs_got=S_FALSE;
                {const LONGLONG start=ticks();
                 while((got=occlusion->GetData(&counted,sizeof counted,D3DGETDATA_FLUSH))==S_FALSE&&seconds(start)<5)Sleep(0);
                 while((needs_got=needs_occlusion->GetData(&needs_counted,sizeof needs_counted,D3DGETDATA_FLUSH))==S_FALSE&&seconds(start)<5)Sleep(0);}
                const UINT mw=x3m::renderer::fog_march_extent(31,scale),mh=x3m::renderer::fog_march_extent(17,scale);
                brackets=brackets&&hr==S_OK&&r.applied&&r.half_width==mw&&r.half_height==mh&&got==S_OK&&needs_got==S_OK&&SUCCEEDED(marks.issue)&&marks.census_begins==1&&marks.census_ends==1&&
                         marks.needs_begins==1&&marks.needs_ends==1&&marks.area==527&&marks.needs_area==527&&marks.needs_scale==scale&&
                         marks.begins[x3m::gpu_sync_timing::FogRepair]==1&&marks.ends[x3m::gpu_sync_timing::FogRepair]==1;
                const auto out=half_image(surface_bytes(d,odd.target_surface.p));const auto lit=half_image(surface_bytes(d,pass.fixture_st()));
                const FogFrame f=make_frame(A,odd,hx.frame,true);const fog_cpu::Setup cpu=cpu_setup(f,hx.config);
                auto class_of=[](const float* p){const bool g=p[0]>=0.f&&p[0]<=1.f,v=p[2]>0.f&&p[2]<=3.402823466e38f;return g?(v?1:2):0;};
                unsigned needs=0,written=0;
                for(UINT y=0;y<17;++y)for(UINT x=0;x<31;++x){
                    const float* dp=&depth[(std::size_t(y)*31+x)*4];const int cd=class_of(dp);const float* g=&out[(std::size_t(y)*31+x)*4];
                    const double hx_=double(x)/scale,hy_=double(y)/scale,bx=std::floor(hx_),by=std::floor(hy_),fx=hx_-bx,fy=hy_-by;
                    double sum[4]{},weight=0;bool compatible=false;
                    for(UINT dy=0;dy<2;++dy)for(UINT dx=0;dx<2;++dx){
                        const double bilinear=(dx?fx:1-fx)*(dy?fy:1-fy);const UINT qx=std::min(UINT(bx)+dx,mw-1),qy=std::min(UINT(by)+dy,mh-1);
                        const float* hp=&depth[(std::size_t(qy*scale)*31+qx*scale)*4];const int ch=class_of(hp);double w=bilinear;
                        if(ch!=cd||ch==2)w=0;else if(cd==1)w*=std::exp(-std::fabs(double(hp[2])-double(dp[2]))/std::max(.05*std::min(double(dp[2]),double(hp[2])),1e-6))+1e-4;
                        compatible=compatible||(bilinear>0&&ch==cd);
                        const float* s=&lit[(std::size_t(qy)*mw+qx)*4];for(int c=0;c<3;++c)sum[c]+=w*s[c];sum[3]+=w*(1-s[3]);weight+=w;
                    }
                    if(cd<2&&!compatible){ // repaired: the full-resolution march through its own raster pixel, or the scene when it is empty
                        ++needs;double v[3];raster_ray(f,x,y,v);const auto m=fog_cpu::march(cpu,v,double(dp[2]));const bool fog=!(m.T==1.0&&m.S[0]==0.0&&m.S[1]==0.0&&m.S[2]==0.0);written+=fog;
                        for(int c=0;c<3;++c)worst_repair=std::max(worst_repair,std::fabs((fog?fog_cpu::apply(scene_rgb[c],m.S[c],m.T,1,double(cpu_look_rows[8][c])):double(half_to_float(odd.colour[c])))-g[c]));
                    }else if(weight>0&&!(sum[0]==0&&sum[1]==0&&sum[2]==0&&sum[3]==0)){
                        for(int c=0;c<3;++c)worst_composite=std::max(worst_composite,std::fabs(fog_cpu::apply(scene_rgb[c],sum[c]/weight,1-sum[3]/weight,1,double(cpu_look_rows[8][c]))-g[c]));
                    }else for(int c=0;c<3;++c)worst_composite=std::max(worst_composite,std::fabs(double(half_to_float(odd.colour[c]))-g[c]));
                }
                needs_of[layout][si]=needs;written_of[layout][si]=written;counted_of[layout][si]=unsigned(counted);needs_counted_of[layout][si]=unsigned(needs_counted);
                counts=counts&&needs_counted==needs&&counted==written;
            }
            odd.release();
        }
        pass.configure_sync_timing(nullptr);
        std::printf("NEEDS_CENSUS a_s2=%u a_s4=%u b_s2=%u b_s4=%u a_s2_twin=%u a_s4_twin=%u b_s2_twin=%u b_s4_twin=%u written_a_s2=%u written_a_s4=%u written_b_s2=%u written_b_s4=%u written_twin_b_s4=%u\n",
                    needs_counted_of[0][0],needs_counted_of[0][1],needs_counted_of[1][0],needs_counted_of[1][1],needs_of[0][0],needs_of[0][1],needs_of[1][0],needs_of[1][1],
                    counted_of[0][0],counted_of[0][1],counted_of[1][0],counted_of[1][1],written_of[1][1]);
        std::printf("Q4_ODD worst_repair_vs_cpu=%.6f worst_composite_vs_twin=%.6f\n",worst_repair,worst_composite);
        require(settled&&brackets,"needs_census_one_bracket_per_frame_at_the_drawn_spacing");
        require(counts,"needs_census_counts_the_pixels_the_repair_marches_at_both_spacings");
        require(needs_of[0][0]==255&&needs_of[0][1]==255&&needs_of[1][0]==0&&needs_of[1][1]==255,"needs_census_layout_b_repaired_only_at_spacing_4");
        require(worst_repair<=1e-3&&worst_composite<=1e-3,"q4_odd_target_composite_and_repair_match_the_cpu_twin_at_both_spacings");
        pass.detach();require(pass.references()==0,"needs_census_detach_releases_everything");
        occlusion.reset();needs_occlusion.reset();require(device_references()==references_before,"needs_census_device_refcount_balanced");
    }
    // --- Dust motes (fog-dust-motes.md): after every existing case, on their own pass instances ---
    dust_motes(d,caps,pp,config,A);
    // --- Capability refusal: the legacy family path is bit-identical with and without a refused density request ---
    {
        auto legacy=[&](bool request_density,std::vector<std::uint8_t>& out,const char*& reason){
            D3DCAPS9 limited=caps;if(request_density)limited.MaxPixelShader30InstructionSlots=511;
            FogPass pass;check(pass.attach(d,table,limited,D3DFMT_X8R8G8B8),"legacy attach");Scene scene(d,caps,128,72);scene.create();
            HRESULT density=S_FALSE;if(request_density){density=pass.prepare_density(config,A.cam,1);reason=pass.density_status().reason;if(pass.fixture_density_cache()||pass.prepare_density(config,A.cam,2)!=D3DERR_NOTAVAILABLE)density=E_FAIL;}
            check(pass.prepare_field(GetModuleHandleA(nullptr),fog_field::Profile::Bluewell),"legacy field");check(pass.prepare_targets(scene.w,scene.h),"legacy targets");
            FogFrame f=make_frame(A,scene,1,false);f.profile=pass.field_profile();f.recipe_id=pass.field_recipe();f.field_generation=pass.field_generation();
            for(int i=0;i<3;++i)f.params.world.origin_mod[i]=float(std::fmod(A.cam[i],32768.0));
            scene.hostile();const Snapshot before(d,caps);FogResult r;const HRESULT hr=pass.execute(f,&r);const Snapshot after(d,caps);state_restorations+=before==after;
            if(request_density){FogFrame g=f;g.density=true;FogResult refused;const auto kept=surface_bytes(d,scene.target_surface.p);if(pass.execute(g,&refused)!=E_INVALIDARG||refused.device_calls!=0||surface_bytes(d,scene.target_surface.p)!=kept)density=E_FAIL;}
            out=surface_bytes(d,scene.target_surface.p);scene.release();return hr==S_OK&&r.applied&&before==after?density:E_FAIL;
        };
        std::vector<std::uint8_t> plain,refused;const char* reason="";const HRESULT a=legacy(false,plain,reason),b=legacy(true,refused,reason);
        std::vector<std::uint16_t> untouched(plain.size()/2);bool fogged=false;std::memcpy(untouched.data(),plain.data(),plain.size());for(std::size_t i=0;i<untouched.size();i+=4)fogged=fogged||untouched[i]!=float_to_half_rne(.25f);
        std::printf("REFUSAL reason=%s legacy_fogged=%u\n",reason,unsigned(fogged));
        require(a==S_FALSE&&b==D3DERR_NOTAVAILABLE&&std::string(reason)=="density_ps30_slots","capability_refusal_is_clean_and_sticky");
        require(fogged&&plain==refused,"refusal_leaves_legacy_fog_output_bit_identical");
    }
    device.reset();api.reset();DestroyWindow(window);
    std::printf("RESULT %s checks=%u failures=%u state_restorations=%u\n",failures?"FAIL":"PASS",checks,failures,state_restorations);
    if(failures)throw std::runtime_error("failed checks");
}
}
int main(int argc,char** argv){
    if(argc!=2){std::printf("usage: fog_density_pass_fixture cases.txt\n");return 2;}
    try{run(argv[1]);return 0;}catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());return 1;}
}
