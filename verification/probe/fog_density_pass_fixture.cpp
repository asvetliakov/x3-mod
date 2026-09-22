// Detached stored-density FogPass fixture (checkpoint 3): the production pass with its cache
// manager, worker, UpdateSurface slab uploads, readiness ramps, Reset and device-loss recovery,
// hostile caller state, the capability refusal beside the legacy path, and CPU references for
// the repair draw, odd target sizes, shafts and the storage seam / lane 3->0 wrap.
// Timings are render-thread CPU under this harness, never game FPS or GPU cost.
#include "../../src/fog/fog_density_cache.h"
#include "fog_density_cpu_march.h"
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
        float pc[128]{},vc[32]{};check(d->GetPixelShaderConstantF(0,pc,32),"state ps constants");check(d->GetVertexShaderConstantF(0,vc,8),"state vs constants");add(pc);add(vc);
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
    // Everything the pass touches (8 pixel samplers, c0..c24, targets, streams) set to values it must put back.
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
        float constants[128];for(unsigned i=0;i<128;++i)constants[i]=float(i)*.25f-7;
        check(d->SetPixelShaderConstantF(0,constants,32),"hostile ps constants");check(d->SetVertexShaderConstantF(0,constants,8),"hostile vs constants");
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
    s.look=cpu_look_rows;return s;
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
    auto device_references=[&]{d->AddRef();return unsigned(d->Release());};const unsigned references_before=device_references();
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

        // --- Odd target, repair and composite against CPU references ---
        {
            Scene odd(d,caps,31,17);std::vector<float> depth(std::size_t(31)*17*4,0.f);FogFrame shape=make_frame(A,odd,0,true);
            for(UINT y=0;y<17;++y)for(UINT x=0;x<31;++x){float* p=&depth[(std::size_t(y)*31+x)*4];if(x%2){double v[3];raster_ray(shape,x,y,v);p[0]=.5f;p[2]=float(60000/std::sqrt(v[0]*v[0]+v[1]*v[1]+1));}else p[0]=2.f;}
            odd.depth_values=depth;odd.create();check(pass.prepare_targets(31,17),"odd targets");require(hx.settle(A.cam),"odd settle");
            FogResult r;const HRESULT hr=hx.execute(A,odd,r);const auto out=half_image(surface_bytes(d,odd.target_surface.p));const auto lit=half_image(surface_bytes(d,pass.fixture_st()));
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
