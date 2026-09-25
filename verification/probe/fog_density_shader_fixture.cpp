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
#include "../../src/renderer/fog_look_math.h"
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
// The single look the renderer draws (FOG_LOOK); the programs above are the unshaped parity reference.
constexpr DWORD look_march_words[]={
#include "../../src/renderer/fog_density_march_look_program_inc.h"
};
constexpr DWORD look_composite_words[]={
#include "../../src/renderer/fog_density_composite_look_program_inc.h"
};
constexpr DWORD look_repair_words[]={
#include "../../src/renderer/fog_density_repair_look_program_inc.h"
};
// The look marched at quarter resolution (fog-gpu-cost.md step C): drawn for the scale-4 reference's cases (scale=4) into a
// 64x36 target of the 256x144 screen, the split and the depth-edge chain; composite and repair read samples 4 px apart.
constexpr DWORD look_march_q4_words[]={
#include "../../src/renderer/fog_density_march_look_q4_program_inc.h"
};
constexpr DWORD look_repair_q4_words[]={
#include "../../src/renderer/fog_density_repair_look_q4_program_inc.h"
};
constexpr DWORD look_composite_q4_words[]={
#include "../../src/renderer/fog_density_composite_look_q4_program_inc.h"
};
// (The 24-far-bin programs of step B and the visibility-grid pass went with --fog-far-bins and --fog-shadow-pass on 2026-09-25.)
constexpr unsigned kRows=x3m::renderer::fog_look_first_register+x3m::renderer::fog_look_rows; // c0..c35
constexpr double kTan30=0.5773502691896257;
struct Case{std::string name;double cam[3],r[3],u[3],f[3];std::string mode,value;bool look=false;unsigned phase=0,shadow=0;float span=120000.f,map=64.f;bool resolved=true;unsigned scale=2;}; // scale: the march spacing (option scale=4, step C) // look: the production look programs instead of the unshaped parity ones // span, map: view depth across and texels of the striped map // shadow 1: dark map, 2: striped occluder along view depth
struct Window{HWND handle=nullptr;~Window(){if(handle)DestroyWindow(handle);}};
LONGLONG ticks(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;}

struct Fixture{
    IDirect3DDevice9* d=nullptr;
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DPixelShader9> march,exact,composite,repair,look_march,look_repair,look_composite;
    Com<IDirect3DPixelShader9> look_march_q4,look_repair_q4,look_composite_q4;
    Com<IDirect3DTexture9> atlas_sys[2],atlas_gpu[2];
    NodeKey origin[2]{};bool resident[2]{};bool empty=false;
    float sigma=0;float chroma[3]{};
    double generate_seconds=0;unsigned generated_atlases=0;

    void create(){
        check(d->CreateVertexShader(reinterpret_cast<const DWORD*>(x3m::renderer::quad_vertex_program()),&vs.p),"vs");
        check(d->CreateVertexDeclaration(x3m::renderer::quad_declaration,&declaration.p),"declaration");
        check(d->CreatePixelShader(march_words,&march.p),"march program");check(d->CreatePixelShader(exact_words,&exact.p),"exact program");
        check(d->CreatePixelShader(composite_words,&composite.p),"composite program");check(d->CreatePixelShader(repair_words,&repair.p),"repair program");
        check(d->CreatePixelShader(look_march_words,&look_march.p),"look march");check(d->CreatePixelShader(look_repair_words,&look_repair.p),"look repair");
        check(d->CreatePixelShader(look_composite_words,&look_composite.p),"look composite");
        check(d->CreatePixelShader(look_march_q4_words,&look_march_q4.p),"look march q4");check(d->CreatePixelShader(look_repair_q4_words,&look_repair_q4.p),"look repair q4");
        check(d->CreatePixelShader(look_composite_q4_words,&look_composite_q4.p),"look composite q4");
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
    IDirect3DPixelShader9* march_for(bool look,bool q4=false)const{return q4?look_march_q4.p:look?look_march.p:march.p;}
    IDirect3DPixelShader9* repair_for(bool look,bool q4=false)const{return q4?look_repair_q4.p:look?look_repair.p:repair.p;}
    IDirect3DPixelShader9* composite_for(bool look,bool q4=false)const{return q4?look_composite_q4.p:look?look_composite.p:composite.p;}
    // c0..c24 for a pose, c25..c35 from the production look constants (default tuning); rays equal the
    // host screen's (x+.5)/half pixel law. `half_w` x `half_h` is the march target, `scale` its spacing (step C: 4 for
    // the quarter programs; the march ray of target pixel p is then still the host law's (p+.5)/half_w).
    void constants(const Case& c,UINT half_w,UINT half_h,float k[kRows][4],unsigned scale=2)const{
        std::memset(k,0,sizeof(float)*4*kRows);const double shift=1.0-1.0/scale; // c0.zw: the march uv (s p+.5)/full to the target pixel centre
        k[0][0]=float(double(half_h)/(double(half_w)*kTan30));k[0][1]=float(1.0/kTan30);k[0][2]=float(-shift/half_w);k[0][3]=float(shift/half_h);
        k[1][0]=float(scale*half_w);k[1][1]=float(scale*half_h);k[1][2]=float(half_w);k[1][3]=float(half_h);
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
        if(c.look)k[2][3]*=x3m::renderer::fog_look_constants(x3m::renderer::FogLookTuning{},chroma,k[8],c.phase,k+x3m::renderer::fog_look_first_register,c.resolved);
        if(c.shadow==2){ // tools/analysis/fog_density_shader_reference.py stripe_visibility: x = 2 z / 120000 - 1, y = 0, depth .5
            k[9][0]=1.f;k[9][1]=.95f;k[9][2]=.85f;k[9][3]=10.f;k[10][2]=2.f/c.span;k[10][3]=-1.f;k[12][3]=.5f;k[13][0]=c.map;k[13][1]=1.f/c.map;k[13][2]=.001f;k[13][3]=1.f;
        }else if(c.shadow){ // every view position maps to the centre of cascade 0 at depth .5; the bound map holds 0: shaft visibility 0
            k[9][0]=1.f;k[9][1]=.95f;k[9][2]=.85f;k[9][3]=10.f;k[12][3]=.5f;k[13][0]=64.f;k[13][1]=1.f/64.f;k[13][2]=.001f;k[13][3]=1.f;
        }
    }
    void state(bool exact_texels){
        const DWORD off[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE};
        for(DWORD s:off)check(d->SetRenderState(static_cast<D3DRENDERSTATETYPE>(s),FALSE),"render state");
        check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"cull");check(d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf),"write mask");
        for(DWORD i:{0u,1u,2u,3u,4u,5u,6u,7u}){
            const DWORD filter=((i==1||i==7)&&!exact_texels)?D3DTEXF_LINEAR:D3DTEXF_POINT;
            check(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"address");check(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"address");
            check(d->SetSamplerState(i,D3DSAMP_MINFILTER,filter),"filter");check(d->SetSamplerState(i,D3DSAMP_MAGFILTER,filter),"filter");check(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"filter");
            check(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE),"srgb");
        }
        check(d->SetTexture(1,atlas_gpu[0].p),"fine");check(d->SetTexture(7,atlas_gpu[1].p),"far");
        check(d->SetVertexShader(vs.p),"vs bind");check(d->SetVertexDeclaration(declaration.p),"declaration bind");
    }
    void draw(IDirect3DSurface9* target,IDirect3DPixelShader9* program,const float k[kRows][4]){
        D3DSURFACE_DESC desc{};check(target->GetDesc(&desc),"target desc");
        check(d->SetRenderTarget(0,target),"target");check(d->SetPixelShader(program),"program");check(d->SetPixelShaderConstantF(0,&k[0][0],kRows),"constants");
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
// Full-resolution depth image: r in [0,1] marks geometry, b is view depth; `half_w` x `half_h` the march target at spacing
// `scale` (full pixel x belongs to target pixel x / scale).
std::vector<float> depth_image(const Case& c,UINT half_w,UINT half_h,unsigned scale=2){
    const UINT w=scale*half_w,h=scale*half_h;std::vector<float> depth(std::size_t(w)*h*4,0.f);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
        float* p=&depth[(std::size_t(y)*w+x)*4];
        if(c.mode=="sky"||c.mode=="empty"){p[0]=2.f;continue;}
        p[0]=.5f;
        if(c.mode=="invalid"){p[2]=c.value=="zero"?0.f:c.value=="nan"?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();continue;}
        // The host witness law: depth_b = requested / |view| at the half pixel centre.
        const double vx=(2*(x/scale+.5)/half_w-1)*(double(half_w)/half_h)*kTan30,vy=(1-2*(y/scale+.5)/half_h)*kTan30;
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
        const std::size_t bytes_per_texel=desc.Format==D3DFMT_A8R8G8B8?4:desc.Format==D3DFMT_A32B32G32R32F?16:8;
        std::uint16_t first,last;std::memcpy(&first,bytes,2);std::memcpy(&last,bytes+std::size_t(desc.Height-1)*lock.Pitch+std::size_t(desc.Width)*bytes_per_texel-2,2);touched=first^last;(void)touched;
        check(sys->UnlockRect(),"sync unlock");
        if(i>=4)ms.push_back(1e3*double(ticks()-start)/double(frequency.QuadPart));
    }
    return percentile(ms,.5);
}
// One cases file: the pose rows, and sigma / chroma into the fixture.
std::vector<Case> read_cases(const std::string& cases_file,float& sigma,float chroma[3]){
    std::ifstream in(cases_file);if(!in)throw std::runtime_error("cases");
    std::vector<Case> cases;std::string line;
    while(std::getline(in,line)){
        std::istringstream row(line);std::string head;if(!(row>>head))continue;
        if(head=="sigma"){row>>sigma;continue;}
        if(head=="chroma"){row>>chroma[0]>>chroma[1]>>chroma[2];continue;}
        Case c;c.name=head;for(double* v:{c.cam,c.r,c.u,c.f})for(int i=0;i<3;++i)row>>v[i];
        row>>c.mode>>c.value;if(!row)throw std::runtime_error("case row "+head);
        for(std::string option;row>>option;){
            unsigned value=0;
            if(option=="look")c.look=true;
            else if(std::sscanf(option.c_str(),"phase=%u",&value)==1)c.phase=value;
            else if(std::sscanf(option.c_str(),"shadow=%u",&value)==1&&value<=2)c.shadow=value;
            else if(std::sscanf(option.c_str(),"resolved=%u",&value)==1)c.resolved=value!=0;
            else if(option=="scale=4")c.scale=4;
            else throw std::runtime_error("case option "+option);
        }
        if(c.scale!=2&&!c.look)throw std::runtime_error("scale=4 is a look case option: "+head);
        cases.push_back(c);
    }
    return cases;
}
// scale4_file (optional): the step C reference's cases, every one scale=4, drawn with the quarter-resolution programs into
// a 64x36 target as <case>.q4_<variant>.f32, the look repair split at spacing 4 (repair_shafts_q4.full.f32) and the
// depth-edge chain at both spacings (edge.*.f32, fog-gpu-cost.md step C).
void run(const std::string& cases_file,const std::string& out,const std::vector<std::string>& variant_files){
    Fixture fx;std::vector<Case> cases=read_cases(cases_file,fx.sigma,fx.chroma);
    if(cases.empty()||!(fx.sigma>0))throw std::runtime_error("no cases");
    for(const Case& c:cases)if(c.scale!=2)throw std::runtime_error("variant case in the default cases file: "+c.name);
    bool variant_q4=false;
    for(const std::string& variant_file:variant_files){
        float sigma=0,chroma[3]{};std::vector<Case> extra=read_cases(variant_file,sigma,chroma);
        if(sigma!=fx.sigma||chroma[0]!=fx.chroma[0]||chroma[1]!=fx.chroma[1]||chroma[2]!=fx.chroma[2])throw std::runtime_error("variant cases: another family");
        if(extra.empty())continue;
        for(const Case& c:extra)if(c.scale!=4)throw std::runtime_error("variant file holds a scale-2 case: "+c.name);
        if(variant_q4)throw std::runtime_error("two scale=4 files");
        variant_q4=true;cases.insert(cases.end(),extra.begin(),extra.end());
    }
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
    // The quarter-resolution march targets of the same 256x144 screen (step C).
    const UINT qw=x3m::renderer::fog_march_extent(2*hw,4),qh=x3m::renderer::fog_march_extent(2*hh,4);
    Com<IDirect3DTexture9> st32q,st16q;check(device->CreateTexture(qw,qh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&st32q.p,nullptr),"st32q");check(device->CreateTexture(qw,qh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&st16q.p,nullptr),"st16q");
    Com<IDirect3DSurface9> st32qs,st16qs;check(st32q->GetSurfaceLevel(0,&st32qs.p),"st32q surface");check(st16q->GetSurfaceLevel(0,&st16qs.p),"st16q surface");
    // A 64x64 R32F map of zeros: with Case::shadow every fogged sample is fully shadowed.
    Com<IDirect3DTexture9> dark_map;{const std::vector<float> zeros(64*64,0.f);upload(device.p,64,64,D3DFMT_R32F,4,zeros.data(),&dark_map.p);}
    // Columns 0 (occluder) in every other pair between 8 and 55, 1 elsewhere: shaft visibility varies along each
    // ray, so the per-pixel offset of the shaft lookup (look_taps.zw) shows in the result (reference: stripe_map).
    // The repair split uses the 1024-texel one (47-unit pairs over its 20000-unit columns, whose far bins are 200 units).
    Com<IDirect3DTexture9> stripe_map,fine_stripe_map;
    for(const int n:{64,1024}){std::vector<float> stripes(std::size_t(n)*n);for(std::size_t i=0;i<stripes.size();++i){const int x=int(i%n);stripes[i]=(x>=n/8&&x<n-n/8&&(x/2)%2==1)?0.f:1.f;}upload(device.p,n,n,D3DFMT_R32F,4,stripes.data(),n==64?&stripe_map.p:&fine_stripe_map.p);}
    bool identity=true,empty_identity=true,shadowed_coloured=true;unsigned shadowed_fogged=0,look_cases=0;
    for(const Case& c:cases){
        const bool q4=c.scale==4;const UINT mw=q4?qw:hw,mh=q4?qh:hh;
        fx.fill(c.cam,c.mode=="empty");float k[kRows][4];fx.constants(c,mw,mh,k,c.scale);
        const auto depth=depth_image(c,mw,mh,c.scale);Com<IDirect3DTexture9> depth_texture;upload(device.p,c.scale*mw,c.scale*mh,D3DFMT_A32B32G32R32F,16,depth.data(),&depth_texture.p);
        IDirect3DTexture9* map0=c.shadow==2?stripe_map.p:c.shadow?dark_map.p:nullptr;
        check(device->SetTexture(0,depth_texture.p),"depth bind");check(device->SetTexture(4,map0),"shadow map bind");
        look_cases+=c.look;
        struct Variant{const char* name;IDirect3DPixelShader9* program;IDirect3DSurface9* target;bool exact;};
        const bool plain=!c.look&&!c.shadow; // the texel-exact parity program has no look or shaft variant
        const Variant variants[]={{"bilinear32",fx.march_for(c.look,q4),q4?st32qs.p:st32s.p,false},{"exact32",fx.exact.p,st32s.p,true},{"bilinear16",fx.march_for(c.look,q4),q4?st16qs.p:st16s.p,false}};
        for(const Variant& v:variants){
            if(v.exact&&!plain)continue;
            check(device->BeginScene(),"begin");fx.state(v.exact);fx.draw(v.target,v.program,k);check(device->EndScene(),"end");
            const auto image=readback(device.p,v.target);write(image,out+"\\"+c.name+"."+(q4?"q4_":"")+v.name+".f32");
            if(c.mode=="invalid"||c.mode=="empty")for(std::size_t i=0;i<image.size();i+=4){
                const bool same=image[i]==0.f&&image[i+1]==0.f&&image[i+2]==0.f&&image[i+3]==1.f;
                (c.mode=="empty"?empty_identity:identity)&=same;
            }
            // Fully shadowed fog under the look: coloured light remains (ambient plus the floors), never the
            // black shaft of the retired unshaped law (run214).
            if(c.shadow==1&&c.look&&!v.exact&&(v.target==st32s.p||v.target==st32qs.p))for(std::size_t i=0;i<image.size();i+=4){
                if(!(image[i+3]<1.f))continue;
                ++shadowed_fogged;
                shadowed_coloured&=image[i]>0.f&&image[i+1]>0.f&&image[i+2]>0.f;
            }
        }
        check(device->SetTexture(0,nullptr),"depth unbind");check(device->SetTexture(4,nullptr),"shadow map unbind");
        std::printf("CASE %s mode=%s look=%u shadow=%u scale=%u\n",c.name.c_str(),c.mode.c_str(),unsigned(c.look),unsigned(c.shadow),c.scale);
    }
    require(identity,"invalid_depth_zero_nan_inf_exact_identity");require(empty_identity,"zero_cache_exact_identity");
    std::printf("SHADOWED look_fogged_pixels=%u look_cases=%u\n",shadowed_fogged,look_cases);
    require(shadowed_fogged>0&&shadowed_coloured,"look_fully_shadowed_fog_is_coloured");
    std::printf("GENERATION atlases=%u seconds=%.6f nodes_per_second=%.0f\n",fx.generated_atlases,fx.generate_seconds,fx.generated_atlases?fx.generated_atlases*double(kWindowNodes)*kWindowNodes*kWindowNodes/fx.generate_seconds:0.);

    // Composite and repair split on the first pose: odd full-resolution columns are geometry,
    // even columns (all half samples) sky, so every odd column has composite weight 0.
    {
        Case c=cases.front();const UINT w=2*hw,h=2*hh;float k[kRows][4];
        std::vector<float> depth(std::size_t(w)*h*4,0.f);std::vector<std::uint16_t> scene(std::size_t(w)*h*4);
        const std::uint16_t colour[4]={float_to_half_rne(.25f),float_to_half_rne(.5f),float_to_half_rne(.75f),float_to_half_rne(.37f)};
        for(std::size_t i=0;i<std::size_t(w)*h;++i){depth[4*i]=(i%w)%2?.5f:2.f;depth[4*i+2]=20000.f;std::memcpy(&scene[4*i],colour,8);}
        Com<IDirect3DTexture9> depth_texture,scene_texture,target,full32;upload(device.p,w,h,D3DFMT_A32B32G32R32F,16,depth.data(),&depth_texture.p);upload(device.p,w,h,D3DFMT_A16B16G16R16F,8,scene.data(),&scene_texture.p);
        check(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&target.p,nullptr),"composite target");check(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&full32.p,nullptr),"full march target");
        Com<IDirect3DSurface9> target_surface,full_surface;check(target->GetSurfaceLevel(0,&target_surface.p),"surface");check(full32->GetSurfaceLevel(0,&full_surface.p),"surface");
        // Once for the unshaped parity programs and once for the look programs (tinted extinction T^k, no sample
        // offset). Then with the striped occluder bound (47-unit stripe pairs over the 20000-unit columns): the look
        // repair program keeps the bin centres (FOG_LOOK_NO_OFFSET) while its march offsets the shaft lookup.
        // With the variant cases: the look split once more with the quarter-resolution march, composite and repair
        // (`_look_shafts_q4`: samples at columns 4q are sky, so the same odd geometry columns are repaired and every even
        // column is served).
        struct Split{bool look;unsigned shadow;const char* tag;bool q4;};
        std::vector<Split> splits{Split{false,0,"",false},Split{true,0,"_look",false},Split{true,2,"_look_shafts",false}};
        if(variant_q4)splits.push_back(Split{true,2,"_look_shafts_q4",true});
        for(const Split& split:splits){
        const bool look=split.look,q4=split.q4;c.look=look;c.shadow=split.shadow;c.phase=split.shadow?5u:0u;c.span=24000.f;c.map=1024.f;const std::string tag=split.tag;
        IDirect3DTexture9* const st_texture=q4?st16q.p:st16.p;IDirect3DSurface9* const st_surface=q4?st16qs.p:st16s.p;
        check(device->SetTexture(4,split.shadow?fine_stripe_map.p:nullptr),"shaft map");
        auto chain=[&](bool want_empty,std::vector<float>& composited,std::vector<float>& repaired){
            fx.fill(c.cam,want_empty);if(q4)fx.constants(c,qw,qh,k,4);else fx.constants(c,hw,hh,k);
            check(device->BeginScene(),"begin");fx.state(false);check(device->SetTexture(0,depth_texture.p),"depth");check(device->SetTexture(2,scene_texture.p),"scene");check(device->SetTexture(3,nullptr),"st unbind");
            fx.draw(st_surface,fx.march_for(look,q4),k);check(device->SetTexture(3,st_texture),"st");fx.draw(target_surface.p,fx.composite_for(look,q4),k);check(device->EndScene(),"end");composited=readback(device.p,target_surface.p);
            check(device->BeginScene(),"begin");fx.draw(target_surface.p,fx.repair_for(look,q4),k);check(device->EndScene(),"end");repaired=readback(device.p,target_surface.p);check(device->SetTexture(3,nullptr),"st unbind");
        };
        std::vector<float> composited,repaired;chain(true,composited,repaired);bool untouched=true;
        for(std::size_t i=0;i<composited.size();i+=4)for(int j=0;j<4;++j){const float expect=half_to_float(colour[j]);untouched&=composited[i+j]==expect&&repaired[i+j]==expect;}
        require(untouched,("empty_cache_composite_and_repair_exact_scene"+tag).c_str());
        chain(false,composited,repaired);
        // Program consistency only: the march program driven at full resolution with the same ray law
        // (sizes doubled, c0 shifted a quarter pixel) must equal what repair wrote. This says nothing about
        // where the ray should go; fog_density_pass_fixture.cpp checks repaired pixels of the production
        // pass against a CPU march through the raster pixel, with a half-pixel-offset control.
        float full[kRows][4];std::memcpy(full,k,sizeof full);full[1][0]=float(2*w);full[1][1]=float(2*h);full[1][2]=float(w);full[1][3]=float(h);full[0][2]=k[0][2]-.5f/float(w);full[0][3]=k[0][3]+.5f/float(h);
        // With shafts the host checker owns parity too (image repair_shafts.full). The march below checks the repair
        // program's bin-centre lookup, which is the march's unresolved constants (c32.zw = 0).
        if(split.shadow){
            write(repaired,out+(q4?"\\repair_shafts_q4.full.f32":"\\repair_shafts.full.f32"));
            Case held=c;held.resolved=false;float kh[kRows][4];if(q4)fx.constants(held,qw,qh,kh,4);else fx.constants(held,hw,hh,kh);std::memcpy(full[x3m::renderer::fog_look_first_register+7],kh[x3m::renderer::fog_look_first_register+7],16);
        }
        check(device->BeginScene(),"begin");fx.state(false);fx.draw(full_surface.p,fx.march_for(look),full);check(device->EndScene(),"end");const auto st=readback(device.p,full_surface.p);
        unsigned changed=0,fogged=0;bool even_kept=true,odd_composite_scene=true,alpha=true;double worst=0;
        for(std::size_t i=0;i<std::size_t(w)*h;++i){
            const bool odd=(i%w)%2;const float* a=&composited[4*i];const float* b=&repaired[4*i];const float* s=&st[4*i];
            alpha&=a[3]==half_to_float(colour[3])&&b[3]==a[3];
            if(!odd){even_kept&=!std::memcmp(a,b,16);continue;}
            for(int j=0;j<3;++j)odd_composite_scene&=a[j]==half_to_float(colour[j]);
            const bool has_fog=!(s[0]==0.f&&s[1]==0.f&&s[2]==0.f&&s[3]==1.f);fogged+=has_fog;changed+=std::memcmp(a,b,16)!=0;
            for(int j=0;j<3;++j){const double T=look?std::pow(double(s[3]),double(k[x3m::renderer::fog_look_first_register+8][j])):double(s[3]);const double expect=has_fog?double(half_to_float(colour[j]))*T+s[j]:half_to_float(colour[j]);worst=std::max(worst,std::fabs(expect-b[j]));}
        }
        if(split.shadow)std::printf("REPAIR_SHAFTS%s odd_pixels=%u fogged=%u changed=%u worst_vs_bin_centre_march=%.9g\n",q4?"_Q4":"",w/2*h,fogged,changed,worst);
        else std::printf("REPAIR%s odd_pixels=%u fogged=%u changed=%u worst_vs_full_march=%.9g\n",look?"_LOOK":"",w/2*h,fogged,changed,worst);
        require(even_kept,("repair_leaves_compatible_pixels_bit_identical"+tag).c_str());require(odd_composite_scene,("composite_keeps_scene_on_zero_weight"+tag).c_str());
        require(alpha,("source_alpha_exact"+tag).c_str());
        require(fogged>0&&changed<=fogged&&worst<=1e-3,("repair_program_consistent_with_march_program"+tag).c_str());
        }
        check(device->SetTexture(4,nullptr),"shaft map unbind");
        check(device->SetTexture(0,nullptr),"unbind");check(device->SetTexture(2,nullptr),"unbind");
    }
    // Step C depth-edge chain (fog-gpu-cost.md): a station-like depth layout on the 256x144 screen of the first pose (a hull,
    // a nearer module across its edge, a far hull, struts 1-6 px wide over sky and over the far hull, a 2-px diagonal cable),
    // drawn the production way at both spacings: march into the FP16 target, composite, repair, into an FP32 scene target
    // over a scene of 0 (out = S) and of 1 (out = T^k + S). The truth is the look marched at every full pixel. c0.zw is the
    // production one here (zero raster offset): the march ray of target pixel p goes through full pixel s p, the repair's and
    // the truth's through the pixel itself. No shadow map (shadow_select.x = 0), so the lookup offset plays no part. The host
    // checker measures how far spacing 4 moves the image from spacing 2 on the depth edges (edge.*.f32, edge.depth.f32).
    if(variant_q4){
        Case c=cases.front();c.look=true;c.shadow=0;c.phase=0;c.resolved=true;c.scale=2;
        const UINT w=2*hw,h=2*hh;std::vector<float> depth(std::size_t(w)*h*4,0.f);
        for(std::size_t i=0;i<std::size_t(w)*h;++i)depth[4*i]=2.f; // sky
        auto geometry=[&](UINT x0,UINT y0,UINT x1,UINT y1,float z){for(UINT y=y0;y<y1&&y<h;++y)for(UINT x=x0;x<x1&&x<w;++x){float* p=&depth[(std::size_t(y)*w+x)*4];p[0]=.5f;p[2]=z;}};
        // Edges at odd and even pixels alike (the spacing-2 samples sit on even ones), as rasterised hulls fall.
        geometry(161,81,250,136,90000.f);  // a far hull
        geometry(41,31,151,110,25000.f);   // the station hull
        geometry(101,61,190,90,12000.f);   // a nearer module across the hull's edge
        {UINT x=197;for(UINT width:{1u,2u,3u,4u,6u}){geometry(x,10,x+width,136,18000.f);x+=width+5;}}  // vertical struts over sky and the far hull
        {UINT y=115;for(UINT width:{1u,2u,3u}){geometry(10,y,190,y+width,18000.f);y+=width+4;}}       // horizontal struts under the hull
        for(UINT x=40;x<150;++x){const double yc=12.0+(x-40)*.12;for(UINT y=0;y<h;++y)if(std::fabs(y+.5-yc)<1.0)geometry(x,y,x+1,y+1,15000.f);} // a 2-px cable
        write(depth,out+"\\edge.depth.f32");
        Com<IDirect3DTexture9> depth_texture,scenes[2],target;Com<IDirect3DSurface9> target_surface;
        upload(device.p,w,h,D3DFMT_A32B32G32R32F,16,depth.data(),&depth_texture.p);
        for(int s=0;s<2;++s){std::vector<std::uint16_t> scene(std::size_t(w)*h*4,float_to_half_rne(s?1.f:0.f));for(std::size_t i=3;i<scene.size();i+=4)scene[i]=float_to_half_rne(.37f);upload(device.p,w,h,D3DFMT_A16B16G16R16F,8,scene.data(),&scenes[s].p);}
        check(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&target.p,nullptr),"edge target");check(target->GetSurfaceLevel(0,&target_surface.p),"edge surface");
        fx.fill(c.cam,false);
        {   // The truth: the march program at every full pixel (sizes 2w x 2h, the target w x h, c0 at the full pixel centre).
            float kf[kRows][4];fx.constants(c,w,h,kf);
            check(device->BeginScene(),"begin");fx.state(false);check(device->SetTexture(0,depth_texture.p),"depth");fx.draw(target_surface.p,fx.march_for(true),kf);check(device->EndScene(),"end");
            write(readback(device.p,target_surface.p),out+"\\edge.truth.f32");
        }
        unsigned changed[2]{};
        for(unsigned scale:{2u,4u}){
            const bool q4=scale==4;float k[kRows][4];fx.constants(c,q4?qw:hw,q4?qh:hh,k,scale);k[0][2]=k[0][3]=0.f; // production c0: the ray through full pixel s p
            IDirect3DTexture9* const st_texture=q4?st16q.p:st16.p;IDirect3DSurface9* const st_surface=q4?st16qs.p:st16s.p;
            std::vector<float> composited[2];
            for(int s=0;s<2;++s){
                check(device->BeginScene(),"begin");fx.state(false);check(device->SetTexture(0,depth_texture.p),"depth");check(device->SetTexture(2,scenes[s].p),"scene");check(device->SetTexture(3,nullptr),"st unbind");
                fx.draw(st_surface,fx.march_for(true,q4),k);check(device->SetTexture(3,st_texture),"st");fx.draw(target_surface.p,fx.composite_for(true,q4),k);check(device->EndScene(),"end");
                composited[s]=readback(device.p,target_surface.p);
                check(device->BeginScene(),"begin");fx.draw(target_surface.p,fx.repair_for(true,q4),k);check(device->EndScene(),"end");
                const auto repaired=readback(device.p,target_surface.p);check(device->SetTexture(3,nullptr),"st unbind");
                write(repaired,out+"\\edge.s"+std::to_string(scale)+".scene"+std::to_string(s)+".f32");
                if(s==0)for(std::size_t i=0;i<repaired.size();i+=4)changed[q4]+=std::memcmp(&repaired[i],&composited[s][i],16)!=0;
                if(s==0)write(readback(device.p,st_surface),out+"\\edge.s"+std::to_string(scale)+".st.f32");
            }
        }
        std::printf("EDGE width=%u height=%u repaired_s2=%u repaired_s4=%u\n",w,h,changed[0],changed[1]);
        require(changed[0]>0&&changed[1]>changed[0],"edge_chain_repairs_more_pixels_at_spacing_4");
        for(DWORD i:{0u,2u,3u})check(device->SetTexture(i,nullptr),"edge unbind");
    }
    // Fixture timing at 1280x768 (half 640x384), first pose, static atlases.
    {
        const Case& c=cases.front();const UINT w=1280,h=768;float k[kRows][4];fx.fill(c.cam,false);fx.constants(c,w/2,h/2,k);
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
        // The look, same slope method (sky depth: the capped 70000 column; depth 29300 keeps both levels busy).
        for(IDirect3DTexture9* depth:{depth_sky.p,depth_near.p}){
            Case lc=c;lc.look=true;lc.phase=3;float lk[kRows][4];fx.constants(lc,w/2,h/2,lk);
            check(device->SetTexture(0,depth),"depth");check(device->SetTexture(3,nullptr),"st unbind");double ms[2];int n=0;
            for(int repeat:{1,10})ms[n++]=synced_median(device.p,st_surface.p,[&]{for(int i=0;i<repeat;++i)fx.draw(st_surface.p,fx.march_for(true),lk);});
            std::printf("FIXTURE_SLOPE_TIMING march_%s_look width=%u height=%u one_march_ms=%.4f ten_marches_ms=%.4f per_march_slope_ms=%.4f not_game_fps=1\n",depth==depth_sky.p?"sky":"depth29300",w,h,ms[0],ms[1],(ms[1]-ms[0])/9.0);
        }
        report("march_sky_132_reads",depth_sky.p,false);report("march_depth29300_172_reads",depth_near.p,false);report("transaction_sky_march_composite_repair",depth_sky.p,true);
        for(DWORD i:{0u,2u,3u})check(device->SetTexture(i,nullptr),"unbind");
    }
    std::printf("RESULT PASS checks=%u\n",checks);
}
}
int main(int argc,char** argv){
    if(argc<3||argc>4){std::printf("usage: fog_density_shader_fixture cases.txt output-directory [scale4-cases.txt]\n");return 2;}
    try{run(argv[1],argv[2],std::vector<std::string>(argv+3,argv+argc));return 0;}catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());return 1;}
}
