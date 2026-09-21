// Detached stored-density fog shader fixture (checkpoint 2, static caches only).
// Fills the two toroidal atlases with the production generator for each exported
// pose, marches with the production bilinear program and the verification-only
// texel-exact program, dumps float (S,T) images for the host checker, checks the
// composite/repair split structurally and times the transaction at 1280x768.
// Fixture timing is GPU transaction time under this harness, not game FPS.
#include "../../src/fog/fog_density_generator.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/quad_vertex_program.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace x3m::fog;
using x3m::renderer::QuadVertex;
template<class T> struct Com{T* p=nullptr;~Com(){reset();}void reset(){if(p)p->Release();p=nullptr;}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;};
void check(HRESULT hr,const char* name){if(FAILED(hr)){std::printf("FAIL api=%s hr=%08lx\n",name,(unsigned long)hr);throw std::runtime_error(name);}}
unsigned checks=0;
void require(bool value,const char* name){++checks;std::printf("CHECK %s %s\n",name,value?"PASS":"FAIL");if(!value)throw std::runtime_error(name);}
constexpr DWORD march_words[]={
#include "../../src/renderer/fog_density_march_program_inc.h"
};
constexpr DWORD composite_words[]={
#include "../../src/renderer/fog_density_composite_program_inc.h"
};
constexpr DWORD repair_words[]={
#include "../../src/renderer/fog_density_repair_program_inc.h"
};
constexpr DWORD exact_words[]={
#include "fog_density_march_exact_program_inc.h"
};
constexpr double kTan30=0.5773502691896257;
struct Case{std::string name;double cam[3],r[3],u[3],f[3];std::string mode,value;};
struct Window{HWND handle=nullptr;~Window(){if(handle)DestroyWindow(handle);}};
LONGLONG ticks(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;}

struct Fixture{
    IDirect3DDevice9* d=nullptr;
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DPixelShader9> march,exact,composite,repair;
    Com<IDirect3DTexture9> atlas_sys[2],atlas_gpu[2];
    NodeKey origin[2]{};bool resident[2]{};bool empty=false;
    float sigma=0;float chroma[3]{};
    double generate_seconds=0;unsigned generated_atlases=0;

    void create(){
        check(d->CreateVertexShader(reinterpret_cast<const DWORD*>(x3m::renderer::quad_vertex_program()),&vs.p),"vs");
        check(d->CreateVertexDeclaration(x3m::renderer::quad_declaration,&declaration.p),"declaration");
        check(d->CreatePixelShader(march_words,&march.p),"march program");check(d->CreatePixelShader(exact_words,&exact.p),"exact program");
        check(d->CreatePixelShader(composite_words,&composite.p),"composite program");check(d->CreatePixelShader(repair_words,&repair.p),"repair program");
        for(int level=0;level<2;++level){
            check(d->CreateTexture(kAtlasWidth,kAtlasHeight,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&atlas_sys[level].p,nullptr),"atlas staging");
            check(d->CreateTexture(kAtlasWidth,kAtlasHeight,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&atlas_gpu[level].p,nullptr),"atlas default");
        }
    }
    // Static cache: all 32 tiles of both levels for the camera's window; zero words for `empty`.
    void fill(const double cam[3],bool want_empty){
        for(int level=0;level<2;++level){
            const NodeKey o=window_origin(kLevelDelta[level],cam[0],cam[1],cam[2]);
            if(resident[level]&&empty==want_empty&&(want_empty||(o.x==origin[level].x&&o.y==origin[level].y&&o.z==origin[level].z)))continue;
            D3DLOCKED_RECT lock{};check(atlas_sys[level]->LockRect(0,&lock,nullptr,0),"atlas lock");
            auto* bytes=static_cast<std::uint8_t*>(lock.pBits);const LONGLONG start=ticks();
            if(want_empty)for(int y=0;y<kAtlasHeight;++y)std::memset(bytes+std::size_t(y)*lock.Pitch,0,kAtlasPitch);
            else for(int group=0;group<kGroupCount;++group)
                generate_tile(kLevelDelta[level],o,kNoOffset,group,bytes+std::size_t(group/kGroupsPerRow)*kTileTexels*lock.Pitch+std::size_t(group%kGroupsPerRow)*kTileTexels*kTexelBytes,std::size_t(lock.Pitch));
            LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
            if(!want_empty){generate_seconds+=double(ticks()-start)/double(frequency.QuadPart);++generated_atlases;}
            check(atlas_sys[level]->UnlockRect(0),"atlas unlock");check(atlas_sys[level]->AddDirtyRect(nullptr),"atlas dirty");
            check(d->UpdateTexture(atlas_sys[level].p,atlas_gpu[level].p),"atlas update");origin[level]=o;resident[level]=true;
        }
        empty=want_empty;
    }
    // c0..c24 for a pose; rays equal the host screen's (x+.5)/half pixel law.
    void constants(const Case& c,UINT half_w,UINT half_h,float k[25][4])const{
        std::memset(k,0,sizeof(float)*100);
        k[0][0]=float(double(half_h)/(double(half_w)*kTan30));k[0][1]=float(1.0/kTan30);k[0][2]=float(-0.5/half_w);k[0][3]=float(0.5/half_h);
        k[1][0]=float(2*half_w);k[1][1]=float(2*half_h);k[1][2]=float(half_w);k[1][3]=float(half_h);
        k[2][3]=sigma;k[3][0]=1.f;k[3][3]=float(kTaperEnd);
        for(int i=0;i<3;++i){k[4+i][0]=float(c.r[i]);k[4+i][1]=float(c.u[i]);k[4+i][2]=float(c.f[i]);}
        k[7][0]=1.09f;k[7][1]=.6f;k[7][2]=.91f;k[7][3]=1.f;k[8][0]=k[8][1]=k[8][2]=k[8][3]=1.f;
        for(int level=0;level<2;++level){
            const double period=kWindowNodes*kLevelDelta[level];
            for(int i=0;i<3;++i)k[22+level][i]=float(c.cam[i]-period*std::floor(c.cam[i]/period+.5));
            k[22+level][3]=float(1.0/kLevelDelta[level]);
        }
        for(int i=0;i<3;++i)k[24][i]=chroma[i];
        k[24][3]=1.f;
    }
    void state(bool exact_texels){
        const DWORD off[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE};
        for(DWORD s:off)check(d->SetRenderState(static_cast<D3DRENDERSTATETYPE>(s),FALSE),"render state");
        check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"cull");check(d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf),"write mask");
        for(DWORD i:{0u,1u,2u,3u,7u}){
            const DWORD filter=((i==1||i==7)&&!exact_texels)?D3DTEXF_LINEAR:D3DTEXF_POINT;
            check(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"address");check(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"address");
            check(d->SetSamplerState(i,D3DSAMP_MINFILTER,filter),"filter");check(d->SetSamplerState(i,D3DSAMP_MAGFILTER,filter),"filter");check(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"filter");
            check(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE),"srgb");
        }
        check(d->SetTexture(1,atlas_gpu[0].p),"fine");check(d->SetTexture(7,atlas_gpu[1].p),"far");
        check(d->SetVertexShader(vs.p),"vs bind");check(d->SetVertexDeclaration(declaration.p),"declaration bind");
    }
    void draw(IDirect3DSurface9* target,IDirect3DPixelShader9* program,const float k[25][4]){
        D3DSURFACE_DESC desc{};check(target->GetDesc(&desc),"target desc");
        check(d->SetRenderTarget(0,target),"target");check(d->SetPixelShader(program),"program");check(d->SetPixelShaderConstantF(0,&k[0][0],25),"constants");
        QuadVertex q[4];x3m::renderer::quad_vertices(desc.Width,desc.Height,q);check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof q[0]),"draw");
    }
};
void upload(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,UINT stride,const void* bytes,IDirect3DTexture9** out){
    Com<IDirect3DTexture9> sys;check(d->CreateTexture(w,h,1,0,format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr),"upload texture");
    D3DLOCKED_RECT lock{};check(sys->LockRect(0,&lock,nullptr,0),"upload lock");
    for(UINT y=0;y<h;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,static_cast<const char*>(bytes)+std::size_t(y)*w*stride,std::size_t(w)*stride);
    check(sys->UnlockRect(0),"upload unlock");check(d->CreateTexture(w,h,1,0,format,D3DPOOL_DEFAULT,out,nullptr),"input texture");check(d->UpdateTexture(sys.p,*out),"update input");
}
// Float image of a 16F or 32F target.
std::vector<float> readback(IDirect3DDevice9* d,IDirect3DSurface9* rt){
    D3DSURFACE_DESC desc{};check(rt->GetDesc(&desc),"readback desc");
    Com<IDirect3DSurface9> sys;check(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr),"readback surface");
    check(d->GetRenderTargetData(rt,sys.p),"readback");D3DLOCKED_RECT lock{};check(sys->LockRect(&lock,nullptr,D3DLOCK_READONLY),"readback lock");
    std::vector<float> out(std::size_t(desc.Width)*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y){
        const auto* row=static_cast<const std::uint8_t*>(lock.pBits)+std::size_t(y)*lock.Pitch;float* to=out.data()+std::size_t(y)*desc.Width*4;
        if(desc.Format==D3DFMT_A32B32G32R32F)std::memcpy(to,row,std::size_t(desc.Width)*16);
        else for(UINT i=0;i<desc.Width*4;++i){std::uint16_t word;std::memcpy(&word,row+2*i,2);to[i]=half_to_float(word);}
    }
    check(sys->UnlockRect(),"readback unlock");return out;
}
void write(const std::vector<float>& values,const std::string& file){
    std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*4));out.close();if(!out)throw std::runtime_error("write "+file);
}
// Full-resolution depth image: r in [0,1] marks geometry, b is view depth.
std::vector<float> depth_image(const Case& c,UINT half_w,UINT half_h){
    const UINT w=2*half_w,h=2*half_h;std::vector<float> depth(std::size_t(w)*h*4,0.f);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
        float* p=&depth[(std::size_t(y)*w+x)*4];
        if(c.mode=="sky"||c.mode=="empty"){p[0]=2.f;continue;}
        p[0]=.5f;
        if(c.mode=="invalid"){p[2]=c.value=="zero"?0.f:c.value=="nan"?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();continue;}
        // The host witness law: depth_b = requested / |view| at the half pixel centre.
        const double vx=(2*(x/2+.5)/half_w-1)*(double(half_w)/half_h)*kTan30,vy=(1-2*(y/2+.5)/half_h)*kTan30;
        p[2]=float(std::stod(c.value)/std::sqrt(vx*vx+vy*vy+1));
    }
    return depth;
}
struct Timing{double completed_median,completed_p95,submit_median,submit_p95;};
double percentile(std::vector<double> v,double p){std::sort(v.begin(),v.end());const double at=p*(v.size()-1);const std::size_t i=std::size_t(at);return i+1<v.size()?v[i]+(at-i)*(v[i+1]-v[i]):v[i];}
template<class F> Timing measure(IDirect3DDevice9* d,F&& frame){
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);Com<IDirect3DQuery9> query;check(d->CreateQuery(D3DQUERYTYPE_EVENT,&query.p),"EVENT query");
    std::vector<double> completed,submit;
    for(int i=0;i<16+64;++i){
        const LONGLONG start=ticks();check(d->BeginScene(),"begin");frame();check(d->EndScene(),"end");check(query->Issue(D3DISSUE_END),"issue");const LONGLONG submitted=ticks();
        const DWORD began=GetTickCount();HRESULT hr;
        while((hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){if(GetTickCount()-began>10000)throw std::runtime_error("EVENT timeout");Sleep(0);}
        check(hr,"EVENT data");const LONGLONG done=ticks();
        if(i>=16){completed.push_back(1e3*double(done-start)/double(frequency.QuadPart));submit.push_back(1e3*double(submitted-start)/double(frequency.QuadPart));}
    }
    return {percentile(completed,.5),percentile(completed,.95),percentile(submit,.5),percentile(submit,.95)};
}
// EVENT completion is not a trustworthy GPU bound on every backend (a translated
// queue may signal before the work ran), so the same frame is also timed to the end
// of a GetRenderTargetData of its target; `baseline` is a Clear plus that readback.
template<class F> double synced_median(IDirect3DDevice9* d,IDirect3DSurface9* target,F&& frame){
    D3DSURFACE_DESC desc{};check(target->GetDesc(&desc),"sync desc");Com<IDirect3DSurface9> sys;check(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr),"sync surface");
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);std::vector<double> ms;
    for(int i=0;i<4+24;++i){
        const LONGLONG start=ticks();check(d->BeginScene(),"begin");frame();check(d->EndScene(),"end");check(d->GetRenderTargetData(target,sys.p),"sync readback");
        // The copy may stay deferred until the bytes are mapped: lock and touch first and last rows.
        D3DLOCKED_RECT lock{};check(sys->LockRect(&lock,nullptr,D3DLOCK_READONLY),"sync lock");
        volatile std::uint16_t touched=0;const auto* bytes=static_cast<const std::uint8_t*>(lock.pBits);
        std::uint16_t first,last;std::memcpy(&first,bytes,2);std::memcpy(&last,bytes+std::size_t(desc.Height-1)*lock.Pitch+std::size_t(desc.Width)*8-2,2);touched=first^last;(void)touched;
        check(sys->UnlockRect(),"sync unlock");
        if(i>=4)ms.push_back(1e3*double(ticks()-start)/double(frequency.QuadPart));
    }
    return percentile(ms,.5);
}
void run(const std::string& cases_file,const std::string& out){
    std::ifstream in(cases_file);if(!in)throw std::runtime_error("cases");
    std::vector<Case> cases;Fixture fx;std::string line;
    while(std::getline(in,line)){
        std::istringstream row(line);std::string head;if(!(row>>head))continue;
        if(head=="sigma"){row>>fx.sigma;continue;}
        if(head=="chroma"){row>>fx.chroma[0]>>fx.chroma[1]>>fx.chroma[2];continue;}
        Case c;c.name=head;for(double* v:{c.cam,c.r,c.u,c.f})for(int i=0;i<3;++i)row>>v[i];
        row>>c.mode>>c.value;if(!row)throw std::runtime_error("case row "+head);cases.push_back(c);
    }
    if(cases.empty()||!(fx.sigma>0))throw std::runtime_error("no cases");
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="x3m-fog-density-shader";RegisterClassA(&cls);
    Window window;window.handle=CreateWindowA(cls.lpszClassName,"Detached stored-density fog shaders",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window.handle)throw std::runtime_error("window");
    Com<IDirect3D9> api;api.p=Direct3DCreate9(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Direct3DCreate9");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window.handle;pp.BackBufferWidth=1280;pp.BackBufferHeight=768;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> device;check(api->CreateDevice(0,D3DDEVTYPE_HAL,window.handle,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"CreateDevice");
    D3DCAPS9 caps{};check(device->GetDeviceCaps(&caps),"caps");
    std::printf("CAPS pixel_shader_version=%08lx max_ps30_instruction_slots=%lu max_texture=%lux%lu\n",(unsigned long)caps.PixelShaderVersion,(unsigned long)caps.MaxPixelShader30InstructionSlots,(unsigned long)caps.MaxTextureWidth,(unsigned long)caps.MaxTextureHeight);
    require(caps.PixelShaderVersion>=D3DPS_VERSION(3,0)&&caps.MaxPixelShader30InstructionSlots>=512,"caps_ps_3_0_512_slots");
    require(caps.MaxTextureWidth>=unsigned(kAtlasWidth)&&caps.MaxTextureHeight>=unsigned(kAtlasHeight)&&!(caps.TextureCaps&D3DPTEXTURECAPS_POW2),"caps_npot_1032x516");
    require(SUCCEEDED(api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,D3DFMT_A16B16G16R16F)),"format_fp16_linear_filter");
    require(SUCCEEDED(api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,D3DFMT_A32B32G32R32F)),"format_fp32_target_fixture_only");
    fx.d=device.p;fx.create();require(true,"programs_created");
    const UINT hw=128,hh=72;
    Com<IDirect3DTexture9> st32,st16;check(device->CreateTexture(hw,hh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&st32.p,nullptr),"st32");check(device->CreateTexture(hw,hh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&st16.p,nullptr),"st16");
    Com<IDirect3DSurface9> st32s,st16s;check(st32->GetSurfaceLevel(0,&st32s.p),"st32 surface");check(st16->GetSurfaceLevel(0,&st16s.p),"st16 surface");
    bool identity=true,empty_identity=true;
    for(const Case& c:cases){
        fx.fill(c.cam,c.mode=="empty");float k[25][4];fx.constants(c,hw,hh,k);
        const auto depth=depth_image(c,hw,hh);Com<IDirect3DTexture9> depth_texture;upload(device.p,2*hw,2*hh,D3DFMT_A32B32G32R32F,16,depth.data(),&depth_texture.p);
        check(device->SetTexture(0,depth_texture.p),"depth bind");
        struct Variant{const char* name;IDirect3DPixelShader9* program;IDirect3DSurface9* target;bool exact;};
        const Variant variants[]={{"bilinear32",fx.march.p,st32s.p,false},{"exact32",fx.exact.p,st32s.p,true},{"bilinear16",fx.march.p,st16s.p,false}};
        for(const Variant& v:variants){
            check(device->BeginScene(),"begin");fx.state(v.exact);fx.draw(v.target,v.program,k);check(device->EndScene(),"end");
            const auto image=readback(device.p,v.target);write(image,out+"\\"+c.name+"."+v.name+".f32");
            if(c.mode=="invalid"||c.mode=="empty")for(std::size_t i=0;i<image.size();i+=4){
                const bool same=image[i]==0.f&&image[i+1]==0.f&&image[i+2]==0.f&&image[i+3]==1.f;
                (c.mode=="empty"?empty_identity:identity)&=same;
            }
        }
        check(device->SetTexture(0,nullptr),"depth unbind");std::printf("CASE %s mode=%s\n",c.name.c_str(),c.mode.c_str());
    }
    require(identity,"invalid_depth_zero_nan_inf_exact_identity");require(empty_identity,"zero_cache_exact_identity");
    std::printf("GENERATION atlases=%u seconds=%.6f nodes_per_second=%.0f\n",fx.generated_atlases,fx.generate_seconds,fx.generated_atlases?fx.generated_atlases*double(kWindowNodes)*kWindowNodes*kWindowNodes/fx.generate_seconds:0.);

    // Composite and repair split on the first pose: odd full-resolution columns are geometry,
    // even columns (all half samples) sky, so every odd column has composite weight 0.
    {
        const Case& c=cases.front();const UINT w=2*hw,h=2*hh;float k[25][4];
        std::vector<float> depth(std::size_t(w)*h*4,0.f);std::vector<std::uint16_t> scene(std::size_t(w)*h*4);
        const std::uint16_t colour[4]={float_to_half_rne(.25f),float_to_half_rne(.5f),float_to_half_rne(.75f),float_to_half_rne(.37f)};
        for(std::size_t i=0;i<std::size_t(w)*h;++i){depth[4*i]=(i%w)%2?.5f:2.f;depth[4*i+2]=20000.f;std::memcpy(&scene[4*i],colour,8);}
        Com<IDirect3DTexture9> depth_texture,scene_texture,target,full32;upload(device.p,w,h,D3DFMT_A32B32G32R32F,16,depth.data(),&depth_texture.p);upload(device.p,w,h,D3DFMT_A16B16G16R16F,8,scene.data(),&scene_texture.p);
        check(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&target.p,nullptr),"composite target");check(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&full32.p,nullptr),"full march target");
        Com<IDirect3DSurface9> target_surface,full_surface;check(target->GetSurfaceLevel(0,&target_surface.p),"surface");check(full32->GetSurfaceLevel(0,&full_surface.p),"surface");
        auto chain=[&](bool want_empty,std::vector<float>& composited,std::vector<float>& repaired){
            fx.fill(c.cam,want_empty);fx.constants(c,hw,hh,k);
            check(device->BeginScene(),"begin");fx.state(false);check(device->SetTexture(0,depth_texture.p),"depth");check(device->SetTexture(2,scene_texture.p),"scene");check(device->SetTexture(3,nullptr),"st unbind");
            fx.draw(st16s.p,fx.march.p,k);check(device->SetTexture(3,st16.p),"st");fx.draw(target_surface.p,fx.composite.p,k);check(device->EndScene(),"end");composited=readback(device.p,target_surface.p);
            check(device->BeginScene(),"begin");fx.draw(target_surface.p,fx.repair.p,k);check(device->EndScene(),"end");repaired=readback(device.p,target_surface.p);check(device->SetTexture(3,nullptr),"st unbind");
        };
        std::vector<float> composited,repaired;chain(true,composited,repaired);bool untouched=true;
        for(std::size_t i=0;i<composited.size();i+=4)for(int j=0;j<4;++j){const float expect=half_to_float(colour[j]);untouched&=composited[i+j]==expect&&repaired[i+j]==expect;}
        require(untouched,"empty_cache_composite_and_repair_exact_scene");
        chain(false,composited,repaired);
        // Program consistency only: the march program driven at full resolution with the same ray law
        // (sizes doubled, c0 shifted a quarter pixel) must equal what repair wrote. This says nothing about
        // where the ray should go; fog_density_pass_fixture.cpp checks repaired pixels of the production
        // pass against a CPU march through the raster pixel, with a half-pixel-offset control.
        float full[25][4];std::memcpy(full,k,sizeof full);full[1][0]=float(2*w);full[1][1]=float(2*h);full[1][2]=float(w);full[1][3]=float(h);full[0][2]=k[0][2]-.5f/float(w);full[0][3]=k[0][3]+.5f/float(h);
        check(device->BeginScene(),"begin");fx.state(false);fx.draw(full_surface.p,fx.march.p,full);check(device->EndScene(),"end");const auto st=readback(device.p,full_surface.p);
        unsigned changed=0,fogged=0;bool even_kept=true,odd_composite_scene=true,alpha=true;double worst=0;
        for(std::size_t i=0;i<std::size_t(w)*h;++i){
            const bool odd=(i%w)%2;const float* a=&composited[4*i];const float* b=&repaired[4*i];const float* s=&st[4*i];
            alpha&=a[3]==half_to_float(colour[3])&&b[3]==a[3];
            if(!odd){even_kept&=!std::memcmp(a,b,16);continue;}
            for(int j=0;j<3;++j)odd_composite_scene&=a[j]==half_to_float(colour[j]);
            const bool has_fog=!(s[0]==0.f&&s[1]==0.f&&s[2]==0.f&&s[3]==1.f);fogged+=has_fog;changed+=std::memcmp(a,b,16)!=0;
            for(int j=0;j<3;++j){const double expect=has_fog?double(half_to_float(colour[j]))*s[3]+s[j]:half_to_float(colour[j]);worst=std::max(worst,std::fabs(expect-b[j]));}
        }
        std::printf("REPAIR odd_pixels=%u fogged=%u changed=%u worst_vs_full_march=%.9g\n",w/2*h,fogged,changed,worst);
        require(even_kept,"repair_leaves_compatible_pixels_bit_identical");require(odd_composite_scene,"composite_keeps_scene_on_zero_weight");
        require(alpha,"source_alpha_exact");require(fogged>0&&changed<=fogged&&worst<=1e-3,"repair_program_consistent_with_march_program");
        check(device->SetTexture(0,nullptr),"unbind");check(device->SetTexture(2,nullptr),"unbind");
    }
    // Fixture timing at 1280x768 (half 640x384), first pose, static atlases.
    {
        const Case& c=cases.front();const UINT w=1280,h=768;float k[25][4];fx.fill(c.cam,false);fx.constants(c,w/2,h/2,k);
        Case sky=c;sky.mode="sky";Case worst=c;worst.mode="depth";worst.value="29300";
        Com<IDirect3DTexture9> depth_sky,depth_near,scene_texture,st,target;
        {const auto image=depth_image(sky,w/2,h/2);upload(device.p,w,h,D3DFMT_A32B32G32R32F,16,image.data(),&depth_sky.p);}
        {const auto image=depth_image(worst,w/2,h/2);upload(device.p,w,h,D3DFMT_A32B32G32R32F,16,image.data(),&depth_near.p);}
        std::vector<std::uint16_t> scene(std::size_t(w)*h*4,float_to_half_rne(.25f));upload(device.p,w,h,D3DFMT_A16B16G16R16F,8,scene.data(),&scene_texture.p);
        check(device->CreateTexture(w/2,h/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&st.p,nullptr),"timing st");check(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&target.p,nullptr),"timing target");
        Com<IDirect3DSurface9> st_surface,target_surface;check(st->GetSurfaceLevel(0,&st_surface.p),"surface");check(target->GetSurfaceLevel(0,&target_surface.p),"surface");
        fx.state(false);check(device->SetTexture(2,scene_texture.p),"scene");
        auto report=[&](const char* name,IDirect3DTexture9* depth,bool whole){
            check(device->SetTexture(0,depth),"depth");
            const Timing t=measure(device.p,[&]{check(device->SetTexture(3,nullptr),"st unbind");fx.draw(st_surface.p,fx.march.p,k);if(whole){check(device->SetTexture(3,st.p),"st");fx.draw(target_surface.p,fx.composite.p,k);fx.draw(target_surface.p,fx.repair.p,k);}});
            std::printf("FIXTURE_TIMING %s width=%u height=%u frames=64 completed_median_ms=%.4f completed_p95_ms=%.4f submit_median_ms=%.4f submit_p95_ms=%.4f not_game_fps=1\n",name,w,h,t.completed_median,t.completed_p95,t.submit_median,t.submit_p95);
        };
        auto synced=[&](const char* name,IDirect3DTexture9* depth,bool whole){
            check(device->SetTexture(0,depth),"depth");IDirect3DSurface9* target=whole?target_surface.p:st_surface.p;
            const double baseline=synced_median(device.p,target,[&]{check(device->SetRenderTarget(0,target),"target");check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1.f,0),"clear");});
            const double total=synced_median(device.p,target,[&]{check(device->SetTexture(3,nullptr),"st unbind");fx.draw(st_surface.p,fx.march.p,k);if(whole){check(device->SetTexture(3,st.p),"st");fx.draw(target_surface.p,fx.composite.p,k);fx.draw(target_surface.p,fx.repair.p,k);}});
            std::printf("FIXTURE_SYNC_TIMING %s width=%u height=%u frames=24 readback_median_ms=%.4f baseline_clear_readback_median_ms=%.4f net_median_ms=%.4f not_game_fps=1\n",name,w,h,total,baseline,total-baseline);
        };
        synced("march_sky_132_reads",depth_sky.p,false);synced("march_depth29300_172_reads",depth_near.p,false);synced("transaction_sky_march_composite_repair",depth_sky.p,true);
        // Slope of N marches per frame: cancels fixed submission and readback overhead.
        for(IDirect3DTexture9* depth:{depth_sky.p,depth_near.p}){
            check(device->SetTexture(0,depth),"depth");check(device->SetTexture(3,nullptr),"st unbind");double ms[2];int n=0;
            for(int repeat:{1,10})ms[n++]=synced_median(device.p,st_surface.p,[&]{for(int i=0;i<repeat;++i)fx.draw(st_surface.p,fx.march.p,k);});
            std::printf("FIXTURE_SLOPE_TIMING %s width=%u height=%u one_march_ms=%.4f ten_marches_ms=%.4f per_march_slope_ms=%.4f not_game_fps=1\n",depth==depth_sky.p?"march_sky_132_reads":"march_depth29300_172_reads",w,h,ms[0],ms[1],(ms[1]-ms[0])/9.0);
        }
        report("march_sky_132_reads",depth_sky.p,false);report("march_depth29300_172_reads",depth_near.p,false);report("transaction_sky_march_composite_repair",depth_sky.p,true);
        for(DWORD i:{0u,2u,3u})check(device->SetTexture(i,nullptr),"unbind");
    }
    std::printf("RESULT PASS checks=%u\n",checks);
}
}
int main(int argc,char** argv){
    if(argc!=3){std::printf("usage: fog_density_shader_fixture cases.txt output-directory\n");return 2;}
    try{run(argv[1],argv[2]);return 0;}catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());return 1;}
}
