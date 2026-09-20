// Real D3D9 resource-reference observer. No game, draw, payload access or renderer.
// Device AddRef/Release hooks only count and forward. Native vtable slots are
// saved before patching; all resource aliases are released exactly once.
#include "lattice_state_capture.h"
#include "motion_output.h"
#include "capture_state.h"
#include "cpu_state.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Ref = ULONG (WINAPI*)(IDirect3DDevice9*);
using GetRT = x3m::lattice_state::GetTarget;
Ref native_addref=nullptr,native_release=nullptr;
GetRT native_rt=nullptr;
unsigned calls_add=0,calls_release=0,event_count=0,overflow=0;
unsigned aliases_acquired=0,aliases_released=0,case_id=0,effective_targets=0;
const char* phase="setup";
struct Event {const char* phase;bool release;ULONG result;};
std::array<Event,8192> events{};
void require(bool good,const char* why){if(!good)throw std::runtime_error(why);}
void api(HRESULT hr,const char* why){if(FAILED(hr)){std::fprintf(stderr,"%s hr=%08lx\n",why,hr);throw std::runtime_error(why);}}
#define API(expression) api((expression),#expression)
void event(bool release,ULONG result){
    if(release)++calls_release;else ++calls_add;
    if(event_count<events.size())events[event_count++]={phase,release,result};else ++overflow;
}
ULONG WINAPI count_addref(IDirect3DDevice9* d){
    x3m::CpuCallBoundary cpu;cpu.before_original();const ULONG result=native_addref(d);cpu.after_original();
    event(false,result);return result;
}
ULONG WINAPI count_release(IDirect3DDevice9* d){
    x3m::CpuCallBoundary cpu;cpu.before_original();const ULONG result=native_release(d);cpu.after_original();
    event(true,result);return result;
}
// Only data slots change; no executable instructions or detour trampolines.
// One fixture-created device/process. Roll back the two shared COM slots before
// its last application Release, including all exception paths.
struct Hook {
    void** table;void* saved[2];bool installed=false;
    explicit Hook(IDirect3DDevice9* d):table(*reinterpret_cast<void***>(d)){
        saved[0]=table[1];saved[1]=table[2];
        native_addref=reinterpret_cast<Ref>(saved[0]);native_release=reinterpret_cast<Ref>(saved[1]);
        native_rt=reinterpret_cast<GetRT>(table[38]);
        DWORD old=0;require(VirtualProtect(table+1,2*sizeof(void*),PAGE_EXECUTE_READWRITE,&old)!=0,"hook protect");
        InterlockedExchangePointer(table+1,reinterpret_cast<void*>(count_addref));
        InterlockedExchangePointer(table+2,reinterpret_cast<void*>(count_release));installed=true;
        DWORD ignored=0;
        if(!VirtualProtect(table+1,2*sizeof(void*),old,&ignored)){
            InterlockedExchangePointer(table+1,saved[0]);InterlockedExchangePointer(table+2,saved[1]);installed=false;
            VirtualProtect(table+1,2*sizeof(void*),old,&ignored);throw std::runtime_error("hook protection restore");
        }
    }
    bool restore(){
        if(!installed)return true;
        DWORD old=0;if(!VirtualProtect(table+1,2*sizeof(void*),PAGE_EXECUTE_READWRITE,&old))return false;
        InterlockedExchangePointer(table+1,saved[0]);InterlockedExchangePointer(table+2,saved[1]);installed=false;
        DWORD ignored=0;const bool protection=VirtualProtect(table+1,2*sizeof(void*),old,&ignored)!=0;
        return protection&&table[1]==saved[0]&&table[2]==saved[1];
    }
    ~Hook(){if(!restore())std::abort();}
};
template<class T> struct Com {
    T* p=nullptr;~Com(){reset();}void reset(){if(p){p->Release();p=nullptr;}}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
std::vector<DWORD> shader(const std::string& path){
    FILE* f=std::fopen(path.c_str(),"rb");require(f!=nullptr,"open retained shader");
    std::fseek(f,0,SEEK_END);long bytes=std::ftell(f);std::rewind(f);
    if(bytes<=0||bytes>65536||bytes%4){std::fclose(f);throw std::runtime_error("shader size");}
    std::vector<DWORD> code(static_cast<size_t>(bytes)/4);
    const bool ok=std::fread(code.data(),1,bytes,f)==static_cast<size_t>(bytes);std::fclose(f);require(ok,"shader read");return code;
}
struct Sample {HRESULT hr[3]{};std::uintptr_t pointers[3]{};unsigned add=0,release=0;};
Sample sample(IDirect3DDevice9* d){
    const char* previous=phase;phase="mrt_sample";Sample out;
    const auto a=calls_add,r=calls_release;IDirect3DSurface9* surfaces[3]{};
    for(unsigned i=0;i<3;++i){out.hr[i]=native_rt(d,i,surfaces+i);out.pointers[i]=reinterpret_cast<std::uintptr_t>(surfaces[i]);if(surfaces[i])++aliases_acquired;}
    for(auto* surface:surfaces)if(surface){surface->Release();++aliases_released;}
    out.add=calls_add-a;out.release=calls_release-r;phase=previous;return out;
}
HRESULT WINAPI effective_target(IDirect3DDevice9* d,DWORD slot,IDirect3DSurface9** out){
    ++effective_targets;return native_rt(d,slot,out);
}
void print_sample(const Sample& s){
    std::printf("{\"hr\":[\"%08lx\",\"%08lx\",\"%08lx\"],\"pointers\":[%lu,%lu,%lu],\"sample_add\":%u,\"sample_release\":%u}",
        s.hr[0],s.hr[1],s.hr[2],static_cast<unsigned long>(s.pointers[0]),static_cast<unsigned long>(s.pointers[1]),static_cast<unsigned long>(s.pointers[2]),s.add,s.release);
}
struct CpuImage {unsigned char x87[108];DWORD mxcsr,error;};
void cpu_image(CpuImage& image){
    image.error=GetLastError();
    asm volatile("fnsave %0\n frstor %0\n stmxcsr %1":"=m"(image.x87),"=m"(image.mxcsr)::"memory");
}
void seed_cpu(){
    const WORD cw=0x077f;const DWORD mxcsr=0x3f80;
    asm volatile("fninit\n fld1\n fldpi\n fldcw %0\n ldmxcsr %1"::"m"(cw),"m"(mxcsr):"memory");
    SetLastError(0x13572468);
}
template<class F> void operation(IDirect3DDevice9* d,const char* name,F&& f,bool optional_notfound=false){
    const auto before=sample(d);phase=name;const auto a=calls_add,r=calls_release;
    // Like the production draw observer, diagnostics execute inside the full
    // existing CPU/LastError envelope. Hook forwards native input/output too.
    seed_cpu();CpuImage incoming{},outgoing{};cpu_image(incoming);
    HRESULT result=S_OK;x3m::call_preserved([&]{result=f();});cpu_image(outgoing);
    require(incoming.error==outgoing.error&&incoming.mxcsr==outgoing.mxcsr&&!std::memcmp(incoming.x87,outgoing.x87,108),"operation CPU/LastError envelope");
    const auto add=calls_add-a,release=calls_release-r;const auto after=sample(d);phase="idle";
    std::printf("{\"type\":\"operation\",\"case\":%u,\"name\":\"%s\",\"hr\":\"%08lx\",\"add\":%u,\"release\":%u,\"cpu_preserved\":true,\"before\":",case_id,name,result,add,release);
    print_sample(before);std::printf(",\"after\":");print_sample(after);std::printf("}\n");if(!(optional_notfound&&result==D3DERR_NOTFOUND))API(result);
    for(unsigned i=0;i<3;++i)require(SUCCEEDED(before.hr[i])&&SUCCEEDED(after.hr[i])&&before.pointers[i]&&before.pointers[i]==after.pointers[i],"count-only observer changed MRT binding");
}
template<class T> void release_alias(IDirect3DDevice9* d,const char* name,T*& p){
    require(p!=nullptr,"missing query alias");operation(d,name,[&]{p->Release();p=nullptr;++aliases_released;return S_OK;});
}
template<class T,class F> void getter(IDirect3DDevice9* d,const char* name,T*& p,F&& f){
    operation(d,name,[&]{const HRESULT hr=f();if(p)++aliases_acquired;return hr;});require(p!=nullptr,"bound getter returned null");
}
void identity(IDirect3DDevice9* d,const char* name,IDirect3DResource9* resource){
    operation(d,name,[&]{std::uint64_t id=0;return x3m::query_resource_id(resource,&id);},true);
}
struct Resources {
    IDirect3DDevice9* d;Com<IDirect3DSurface9> back,rt0,rt1,rt2,depth;
    Com<IDirect3DTexture9> rt_texture,texture;
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;Com<IDirect3DVertexDeclaration9> decl;
    Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    explicit Resources(IDirect3DDevice9* device):d(device){}
    void setup(const std::vector<DWORD>& vertex,const std::vector<DWORD>& pixel){
        API(d->GetRenderTarget(0,&back.p));
        API(d->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&rt_texture.p,nullptr));
        API(rt_texture.p->GetSurfaceLevel(0,&rt0.p));
        API(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt1.p,nullptr));
        API(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt2.p,nullptr));
        API(d->CreateDepthStencilSurface(64,64,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,FALSE,&depth.p,nullptr));
        API(d->CreateVertexShader(vertex.data(),&vs.p));API(d->CreatePixelShader(pixel.data(),&ps.p));
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_POSITION,0},
            {0,8,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_TEXCOORD,0},{0,16,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_TANGENT,0},{0,32,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_BINORMAL,0},D3DDECL_END()};
        API(d->CreateVertexDeclaration(elements,&decl.p));
        API(d->CreateVertexBuffer(120,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&vb.p,nullptr));
        API(d->CreateIndexBuffer(6,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr));
        API(d->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture.p,nullptr));
        API(d->SetDepthStencilSurface(nullptr));API(d->SetRenderTarget(0,rt0.p));API(d->SetRenderTarget(1,rt1.p));API(d->SetRenderTarget(2,rt2.p));
        API(d->SetDepthStencilSurface(depth.p));API(d->SetVertexShader(vs.p));API(d->SetPixelShader(ps.p));
        API(d->SetVertexDeclaration(decl.p));API(d->SetStreamSource(0,vb.p,0,40));API(d->SetIndices(ib.p));
        API(d->SetTexture(0,texture.p));API(d->SetTexture(3,texture.p));
    }
    void drop(){vs.reset();ps.reset();decl.reset();vb.reset();ib.reset();texture.reset();rt0.reset();rt1.reset();rt2.reset();depth.reset();rt_texture.reset();}
    ~Resources(){
        phase="cleanup";d->SetTexture(0,nullptr);d->SetTexture(3,nullptr);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);
        d->SetVertexDeclaration(nullptr);d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);
        d->SetRenderTarget(1,nullptr);d->SetRenderTarget(2,nullptr);d->SetDepthStencilSurface(nullptr);
        if(back.p)d->SetRenderTarget(0,back.p);
    }
};
void queries(IDirect3DDevice9* d){
    IDirect3DVertexShader9* vs=nullptr;getter(d,"vs.get",vs,[&]{return d->GetVertexShader(&vs);});
    std::array<DWORD,16384> words{};UINT bytes=0;
    operation(d,"vs.size",[&]{return vs->GetFunction(nullptr,&bytes);});require(bytes<=sizeof words,"VS capacity");
    operation(d,"vs.function",[&]{return vs->GetFunction(words.data(),&bytes);});release_alias(d,"vs.release",vs);
    IDirect3DPixelShader9* ps=nullptr;getter(d,"ps.get",ps,[&]{return d->GetPixelShader(&ps);});bytes=0;
    operation(d,"ps.size",[&]{return ps->GetFunction(nullptr,&bytes);});require(bytes<=sizeof words,"PS capacity");
    operation(d,"ps.function",[&]{return ps->GetFunction(words.data(),&bytes);});release_alias(d,"ps.release",ps);
    IDirect3DVertexDeclaration9* decl=nullptr;getter(d,"decl.get",decl,[&]{return d->GetVertexDeclaration(&decl);});
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=MAXD3DDECLLENGTH+1;
    operation(d,"decl.description",[&]{return decl->GetDeclaration(elements,&count);});release_alias(d,"decl.release",decl);
    IDirect3DVertexBuffer9* vb=nullptr;UINT offset=0,stride=0,freq=0;
    getter(d,"stream.get",vb,[&]{return d->GetStreamSource(0,&vb,&offset,&stride);});
    identity(d,"stream.identity",vb);
    operation(d,"stream.frequency",[&]{return d->GetStreamSourceFreq(0,&freq);});D3DVERTEXBUFFER_DESC vd{};
    operation(d,"stream.description",[&]{return vb->GetDesc(&vd);});release_alias(d,"stream.release",vb);
    IDirect3DIndexBuffer9* ib=nullptr;getter(d,"indices.get",ib,[&]{return d->GetIndices(&ib);});D3DINDEXBUFFER_DESC id{};
    identity(d,"indices.identity",ib);
    operation(d,"indices.description",[&]{return ib->GetDesc(&id);});release_alias(d,"indices.release",ib);
    for(unsigned slot=0;slot<4;++slot){
        // Fixed names keep callback event pointers valid until end-of-case dump.
        const char* get_names[]={"rt0.get","rt1.get","rt2.get","depth.get"};
        const char* identity_names[]={"rt0.identity","rt1.identity","rt2.identity","depth.identity"};
        const char* desc_names[]={"rt0.description","rt1.description","rt2.description","depth.description"};
        const char* parent_names[]={"rt0.container","rt1.container","rt2.container","depth.container"};
        const char* parent_release[]={"rt0.container_release","rt1.container_release","rt2.container_release","depth.container_release"};
        const char* release_names[]={"rt0.release","rt1.release","rt2.release","depth.release"};
        IDirect3DSurface9* surface=nullptr;getter(d,get_names[slot],surface,[&]{return slot==3?d->GetDepthStencilSurface(&surface):native_rt(d,slot,&surface);});
        identity(d,identity_names[slot],surface);D3DSURFACE_DESC sd{};operation(d,desc_names[slot],[&]{return surface->GetDesc(&sd);});
        IDirect3DBaseTexture9* parent=nullptr;HRESULT container_hr=S_OK;
        operation(d,parent_names[slot],[&]{container_hr=surface->GetContainer(IID_IDirect3DBaseTexture9,reinterpret_cast<void**>(&parent));if(parent)++aliases_acquired;return SUCCEEDED(container_hr)||container_hr==E_NOINTERFACE?S_OK:container_hr;});
        std::printf("{\"type\":\"container\",\"case\":%u,\"slot\":%u,\"hr\":\"%08lx\",\"present\":%s}\n",case_id,slot,container_hr,parent?"true":"false");
        require(slot!=0||parent,"texture RT0 container missing");if(parent){identity(d,"container.identity",parent);release_alias(d,parent_release[slot],parent);}release_alias(d,release_names[slot],surface);
    }
    for(unsigned stage:{0u,3u}){
        IDirect3DBaseTexture9* texture=nullptr;getter(d,stage?"tex3.get":"tex0.get",texture,[&]{return d->GetTexture(stage,&texture);});
        identity(d,stage?"tex3.identity":"tex0.identity",texture);
        operation(d,stage?"tex3.description":"tex0.description",[&]{require(texture->GetType()==D3DRTYPE_TEXTURE&&texture->GetLOD()==0&&texture->GetLevelCount()==1,"texture metadata");D3DSURFACE_DESC td{};return static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&td);});
        release_alias(d,stage?"tex3.release":"tex0.release",texture);
    }
}
void idle(IDirect3DDevice9* d){
    operation(d,"idle",[]{Sleep(10);return S_OK;});
}
}
// The real Capture::effective has no object-scope dependency. The full helper
// object file also contains selector/publisher symbols; these stubs must never
// execute and are not fabricated COM endpoints or callback evidence.
namespace x3m {void log(const char*,...){std::abort();}namespace object_trace {bool current(Snapshot*,bool){std::abort();}}}
int main(int argc,char** argv){
    HWND window=nullptr;
    try{
        require(argc==2,"usage: lattice_observer_release_fixture.exe retained-shader-directory");
        const auto vertex=shader(std::string(argv[1])+"/vertex.bin"),pixel=shader(std::string(argv[1])+"/candidate.bin");
        window=CreateWindowExA(0,"STATIC","lattice observer diagnostic",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
        require(window!=nullptr,"hidden fixture window");
        {
            Com<IDirect3D9> api9;api9.p=Direct3DCreate9(D3D_SDK_VERSION);require(api9.p!=nullptr,"Direct3DCreate9");
            D3DCAPS9 caps{};API(api9.p->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps));
            require(caps.NumSimultaneousRTs>=3&&caps.VertexShaderVersion>=D3DVS_VERSION(3,0)&&caps.PixelShaderVersion>=D3DPS_VERSION(3,0),"SM3/MRT3");
            D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
            pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
            Com<IDirect3DDevice9> device;API(api9.p->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device.p));
            Hook hook(device.p);
            std::printf("{\"type\":\"device\",\"schema\":1,\"mrt\":%lu,\"real_d3d9\":true,\"selector_bypassed\":true,\"draws\":0,\"production_release_hook\":false}\n",caps.NumSimultaneousRTs);
            // Positive control: exactly one legitimate additional device ref,
            // paired release. Never release a resource through a stale alias.
            phase="hook_control";const auto a=calls_add,r=calls_release;
            device.p->AddRef();device.p->Release();require(calls_add==a+1&&calls_release==r+1,"hook positive control");
            std::printf("{\"type\":\"hook_control\",\"add\":1,\"release\":1}\n");
            for(unsigned cycle=0;cycle<2;++cycle){
                if(cycle){phase="reset";API(device.p->Reset(&pp));std::printf("{\"type\":\"reset\",\"hr\":\"00000000\"}\n");}
                for(unsigned dropped=0;dropped<2;++dropped){
                    case_id=cycle*2+dropped;event_count=overflow=0;aliases_acquired=aliases_released=0;phase="setup";
                    {
                        Resources resources(device.p);resources.setup(vertex,pixel);if(dropped){phase="drop_creation_refs";resources.drop();}
                        std::printf("{\"type\":\"case\",\"case\":%u,\"cycle\":%u,\"ownership\":\"%s\"}\n",case_id,cycle,dropped?"dropped_bound":"held");
                        idle(device.p);queries(device.p);
                        auto capture=std::make_unique<x3m::lattice_state::Capture>();capture->arm(1,case_id,cycle,true);
                        x3m::MotionRoute route;route.submit=true;
                        effective_targets=0;
                        operation(device.p,"effective",[&]{capture->effective(0,device.p,caps,effective_target,route);return S_OK;});
                        require(effective_targets==std::min<DWORD>(caps.NumSimultaneousRTs,4),"actual effective target reachability");
                        std::printf("{\"type\":\"effective_reached\",\"case\":%u,\"target_calls\":%u}\n",case_id,effective_targets);
                        capture->invalidate();idle(device.p);
                        require(aliases_acquired==aliases_released,"explicit alias balance");
                    }
                    for(unsigned i=0;i<event_count;++i)std::printf("{\"type\":\"callback\",\"case\":%u,\"phase\":\"%s\",\"method\":\"%s\",\"result\":%lu}\n",case_id,events[i].phase,events[i].release?"release":"addref",events[i].result);
                    require(!overflow,"callback buffer overflow");
                    std::printf("{\"type\":\"case_end\",\"case\":%u,\"aliases_acquired\":%u,\"aliases_released\":%u,\"events\":%u,\"overflow\":%u}\n",case_id,aliases_acquired,aliases_released,event_count,overflow);
                }
            }
            require(hook.restore(),"hook rollback");std::printf("{\"type\":\"rollback\",\"restored\":true}\n");
            const ULONG final_refs=device.p->Release();device.p=nullptr;
            require(final_refs==0,"final device reference balance");
            std::printf("{\"type\":\"final_release\",\"references\":%lu}\n",final_refs);
        }
        DestroyWindow(window);window=nullptr;std::printf("{\"type\":\"result\",\"status\":\"pass\",\"cases\":4}\n");return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"lattice_observer_release: %s\n",error.what());if(window)DestroyWindow(window);return 1;}
}
