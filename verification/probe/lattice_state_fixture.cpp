// Standalone mock-COM execution of the actual state capture helper and extracted
// production draw_indexed body. No device creation, renderer, payload locks or game.
#include "lattice_state_capture.h"
#include "motion_output.h"
#include "cpu_state.h"
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <type_traits>
using namespace x3m;
namespace {
unsigned checks=0,failures=0,native_calls=0,saved_rt_calls=0,logical_rt_calls=0,forbidden_calls=0;
void check(bool b,const char* why){++checks;if(!b){++failures;std::printf("FAIL %s\n",why);}}
struct Image {unsigned char x87[108];DWORD mxcsr,error;};
void image(Image& o){o.error=GetLastError();asm volatile("fnsave %0\n frstor %0\n stmxcsr %1":"=m"(o.x87),"=m"(o.mxcsr)::"memory");}
void seed(bool output){const WORD cw=output?0x0b7f:0x077f;const DWORD mx=output?0x5f80:0x3f80;
    asm volatile("fninit\n fld1\n fldpi\n fldcw %0\n ldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(output?0x24681357:0x13572468);}
void corrupt(){const DWORD mx=0x1f80;asm volatile("fninit\n fldz\n ldmxcsr %0"::"m"(mx):"memory");SetLastError(0xdeadbeef);}
bool same(const Image& a,const Image& b){return a.mxcsr==b.mxcsr&&a.error==b.error&&!std::memcmp(a.x87,b.x87,108);}
Image expected_input{},expected_output{};
struct Resource {void** table=nullptr;ULONG refs=1;unsigned kind=0;std::vector<DWORD>* code=nullptr;};
std::array<void*,119> device_table{};std::array<void*,22> resource_table{},shader_table{},decl_table{},surface_table{},texture_table{};
struct FakeDevice {void** table=device_table.data();} device;
Resource vb,ib,vs,ps,bad_ps,decl,target,depth,texture;
std::vector<DWORD> vertex_code,pixel_code,bad_code{0xffff0300,0x0000ffff};
bool wrong_shader=false,wrong_declaration=false,fail_render=false,fail_stream=false,submit=true;
int nested_mode=0;bool nested_done=false,nested_publish=false;int nested_original=0;
HRESULT backend_result=S_OK;unsigned group=0,destruction_mode=0;bool destroyed_during_native=false;std::uint64_t frame=0;
std::wstring output_directory;std::shared_ptr<lattice_state::Capture> active;
std::vector<std::string> logs;
ULONG WINAPI addref(Resource* r){corrupt();return ++r->refs;}
ULONG WINAPI release(Resource* r){corrupt();return --r->refs;}
HRESULT WINAPI private_data(Resource*,REFGUID,void*,DWORD*){corrupt();return D3DERR_NOTFOUND;}
D3DRESOURCETYPE WINAPI type(Resource* r){corrupt();return r==&texture?D3DRTYPE_TEXTURE:r==&ib?D3DRTYPE_INDEXBUFFER:r==&vb?D3DRTYPE_VERTEXBUFFER:D3DRTYPE_SURFACE;}
HRESULT WINAPI function(Resource* r,void* out,UINT* size){corrupt();const UINT n=UINT(r->code->size()*4);if(!out){*size=n;return S_OK;}if(*size<n)return D3DERR_INVALIDCALL;std::memcpy(out,r->code->data(),n);*size=n;return S_OK;}
HRESULT WINAPI declaration(Resource*,D3DVERTEXELEMENT9* out,UINT* count){corrupt();if(*count<6)return D3DERR_INVALIDCALL;
    for(unsigned i=0;i<5;++i)out[i]={0,WORD(i*8),D3DDECLTYPE_FLOAT16_4,0,BYTE(i==0?0:i==1?5:i==2?3:i==3?6:7),0};
    out[5]=D3DDECL_END();
    if(wrong_declaration)out[0].Offset=8;
    *count=6;return S_OK;}
HRESULT WINAPI vb_desc(Resource*,D3DVERTEXBUFFER_DESC* out){corrupt();*out={D3DFMT_VERTEXDATA,D3DRTYPE_VERTEXBUFFER,D3DUSAGE_WRITEONLY,D3DPOOL_MANAGED,group?50680u:387200u,0};return S_OK;}
HRESULT WINAPI ib_desc(Resource*,D3DINDEXBUFFER_DESC* out){corrupt();*out={D3DFMT_INDEX16,D3DRTYPE_INDEXBUFFER,D3DUSAGE_WRITEONLY,D3DPOOL_MANAGED,group?5640u:22704u};return S_OK;}
HRESULT WINAPI surface_desc(Resource* r,D3DSURFACE_DESC* out){corrupt();*out={r==&depth?D3DFMT_D24X8:D3DFMT_A8R8G8B8,D3DRTYPE_SURFACE,0,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,1280,768};return S_OK;}
HRESULT WINAPI container(Resource* r,REFIID,void** out){corrupt();*out=nullptr;if(r==&target){addref(&texture);*out=&texture;return S_OK;}return E_NOINTERFACE;}
DWORD WINAPI lod(Resource*){corrupt();return 0;}
DWORD WINAPI levels(Resource*){corrupt();return 2;}
HRESULT WINAPI level_desc(Resource*,UINT level,D3DSURFACE_DESC* out){corrupt();*out={D3DFMT_A8R8G8B8,D3DRTYPE_SURFACE,0,D3DPOOL_MANAGED,D3DMULTISAMPLE_NONE,0,4u>>level,4u>>level};return S_OK;}
HRESULT WINAPI get_vs(FakeDevice*,IDirect3DVertexShader9** out){addref(&vs);*out=reinterpret_cast<IDirect3DVertexShader9*>(&vs);return S_OK;}
HRESULT WINAPI get_ps(FakeDevice*,IDirect3DPixelShader9** out){auto* r=wrong_shader?&bad_ps:&ps;addref(r);*out=reinterpret_cast<IDirect3DPixelShader9*>(r);return S_OK;}
HRESULT WINAPI get_decl(FakeDevice*,IDirect3DVertexDeclaration9** out){addref(&decl);*out=reinterpret_cast<IDirect3DVertexDeclaration9*>(&decl);return S_OK;}
HRESULT WINAPI constants_f(FakeDevice*,UINT,float* out,UINT count){corrupt();std::memset(out,0,count*16);return S_OK;}
HRESULT WINAPI constants_i(FakeDevice*,UINT,int* out,UINT count){corrupt();std::memset(out,0,count*16);return S_OK;}
HRESULT WINAPI constants_b(FakeDevice*,UINT,BOOL* out,UINT count){corrupt();std::memset(out,0,count*4);return S_OK;}
HRESULT WINAPI render(FakeDevice*,D3DRENDERSTATETYPE state,DWORD* out){corrupt();*out=0x12345678;return fail_render&&state==D3DRS_DEPTHBIAS?E_FAIL:S_OK;}
HRESULT WINAPI sampler(FakeDevice*,DWORD,D3DSAMPLERSTATETYPE,DWORD* out){corrupt();*out=0;return S_OK;}
HRESULT WINAPI viewport(FakeDevice*,D3DVIEWPORT9* out){corrupt();*out={0,0,1280,768,0.f,1.f};
    if(nested_mode&&!nested_done){nested_done=true;if(nested_mode==1)nested_publish=active->publish(output_directory.c_str());
        else if(nested_mode==2)nested_original=active->original(reinterpret_cast<IDirect3DDevice9*>(&device),{4,3784,0,9680,0,0},99);
        else active->invalidate();}
    return S_OK;}
HRESULT WINAPI scissor(FakeDevice*,RECT* out){corrupt();*out={0,0,1280,768};return S_OK;}
HRESULT WINAPI clip(FakeDevice*,DWORD,float* out){corrupt();std::memset(out,0,16);return S_OK;}
HRESULT WINAPI stream(FakeDevice*,UINT slot,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride){corrupt();*out=nullptr;if(fail_stream&&!slot)return E_FAIL;
    *offset=0;*stride=slot?0:40;if(!slot){addref(&vb);*out=reinterpret_cast<IDirect3DVertexBuffer9*>(&vb);}return S_OK;}
HRESULT WINAPI frequency(FakeDevice*,UINT,UINT* out){corrupt();*out=1;return S_OK;}
HRESULT WINAPI indices(FakeDevice*,IDirect3DIndexBuffer9** out){addref(&ib);*out=reinterpret_cast<IDirect3DIndexBuffer9*>(&ib);return S_OK;}
HRESULT WINAPI saved_rt(IDirect3DDevice9*,DWORD slot,IDirect3DSurface9** out){++saved_rt_calls;corrupt();*out=nullptr;if(slot)return D3DERR_NOTFOUND;addref(&target);*out=reinterpret_cast<IDirect3DSurface9*>(&target);return S_OK;}
HRESULT WINAPI logical_rt(FakeDevice*,DWORD,IDirect3DSurface9** out){++logical_rt_calls;corrupt();*out=nullptr;return E_FAIL;}
HRESULT WINAPI get_depth(FakeDevice*,IDirect3DSurface9** out){addref(&depth);*out=reinterpret_cast<IDirect3DSurface9*>(&depth);return S_OK;}
HRESULT WINAPI get_texture(FakeDevice*,DWORD,IDirect3DBaseTexture9** out){addref(&texture);*out=reinterpret_cast<IDirect3DBaseTexture9*>(&texture);return S_OK;}
void WINAPI forbidden(){++forbidden_calls;std::abort();}
template<class F> void set(std::array<void*,119>& v,unsigned i,F f){v[i]=reinterpret_cast<void*>(f);}
template<class F> void set(std::array<void*,22>& v,unsigned i,F f){v[i]=reinterpret_cast<void*>(f);}
void initialize(){
    device_table.fill(reinterpret_cast<void*>(forbidden));resource_table.fill(reinterpret_cast<void*>(forbidden));
    for(auto* t:{&resource_table,&shader_table,&decl_table,&surface_table,&texture_table}){t->fill(reinterpret_cast<void*>(forbidden));set(*t,1,addref);set(*t,2,release);}
    set(resource_table,5,private_data);set(resource_table,10,type);set(resource_table,13,vb_desc);
    vb.table=resource_table.data();ib.table=resource_table.data();
    // IB has a distinct documented GetDesc signature at the same slot.
    static auto index_table=resource_table;set(index_table,13,ib_desc);ib.table=index_table.data();
    set(shader_table,4,function);vs.table=ps.table=bad_ps.table=shader_table.data();vs.code=&vertex_code;ps.code=&pixel_code;bad_ps.code=&bad_code;
    set(decl_table,4,declaration);decl.table=decl_table.data();
    set(surface_table,5,private_data);set(surface_table,10,type);set(surface_table,11,container);set(surface_table,12,surface_desc);target.table=depth.table=surface_table.data();
    set(texture_table,5,private_data);set(texture_table,10,type);set(texture_table,12,lod);set(texture_table,13,levels);set(texture_table,17,level_desc);texture.table=texture_table.data();
    set(device_table,38,logical_rt);set(device_table,40,get_depth);set(device_table,48,viewport);set(device_table,56,clip);set(device_table,58,render);set(device_table,64,get_texture);set(device_table,68,sampler);set(device_table,76,scissor);
    set(device_table,88,get_decl);set(device_table,93,get_vs);set(device_table,95,constants_f);set(device_table,97,constants_i);set(device_table,99,constants_b);set(device_table,101,stream);set(device_table,103,frequency);set(device_table,105,indices);set(device_table,108,get_ps);set(device_table,110,constants_f);set(device_table,112,constants_i);set(device_table,114,constants_b);
}
bool read_shader(const char* path,std::vector<DWORD>& out){FILE* f=std::fopen(path,"rb");if(!f)return false;std::fseek(f,0,SEEK_END);const long bytes=std::ftell(f);std::rewind(f);if(bytes<=0||bytes%4||bytes>65536){std::fclose(f);return false;}out.resize(std::size_t(bytes)/4);const bool ok=std::fread(out.data(),1,bytes,f)==std::size_t(bytes);std::fclose(f);return ok;}
}
namespace x3m {
void log(const char* format,...){char text[512];va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);logs.emplace_back(text);}
namespace object_trace {
bool current(Snapshot* out,bool){corrupt();*out={};out->valid=127;out->scope_depth=1;out->session=7;out->model=0x54b3;out->node=0x1000;out->node_handle=10;
    std::memcpy(out->position,lattice_state::position,12);return true;}
}
}
// Production hook surroundings; helper, COM operations and CPU boundaries above
// are real compiled implementations. These do not create GPU state or resources.
namespace x3m::ownership {int process_admission_monitor(){return 0;}struct ApplicationAdmissionAbi{explicit ApplicationAdmissionAbi(int){}};}
namespace frame_timing {enum class Bucket{Draw};void draw_native_begin(){}void draw_native_end(){}}
namespace x3m::telemetry {enum class Metric{DrawBackend};bool enabled(Metric){return false;}void record(int,Metric,int,bool){}}
struct PlainHookGuard{explicit PlainHookGuard(frame_timing::Bucket){}};
struct CallTimer{int backend_ticks=0;template<class T>CallTimer(T&,bool){}void begin(){}void end(){}};
struct DrawCall{bool a,b;D3DPRIMITIVETYPE t;UINT c,s;INT base;UINT m,n;bool composition=false;};
struct Motion{bool draw_submission_blocked(){return false;}bool composition_requested(){return false;}bool reference_accounting_busy(){return false;}void restore_bindings(){}
    MotionRoute before_draw(DrawCall){MotionRoute r;r.submit=submit;r.routed=true;r.matched=true;r.scene=true;r.depth=true;r.submission_error=D3DERR_INVALIDCALL;return r;}
    void after_draw(MotionRoute&,HRESULT){}};
struct Depth{void before_draw(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT){}void after_draw(HRESULT){}};
void nested_present_on_native();
HRESULT WINAPI native_draw(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c){
    Image got{};image(got);check(same(got,expected_input),"native incoming CPU/LastError");
    check(d==reinterpret_cast<IDirect3DDevice9*>(&device)&&t==D3DPT_TRIANGLELIST&&!b&&!m&&!s&&n==(group?1267u:9680u)&&c==(group?940u:3784u),"native exact arguments");
    ++native_calls;if(destruction_mode&&group==1)nested_present_on_native();
    seed(true);image(expected_output);return backend_result;
}
ULONG fixture_device_refs=1;
ULONG WINAPI fixture_native_addref(IDirect3DDevice9*){return ++fixture_device_refs;}
ULONG release_device(IDirect3DDevice9*){return --fixture_device_refs;}
struct Device{int stats=0;D3DCAPS9 caps{};Motion motion_output;Depth scene_depth;bool capture=false;
    bool composition_scene_owner=false,reset_active=false,compositor=false;DWORD scene_thread=0;unsigned bloom_busy=0,composition_draw_depth=0;
    unsigned lattice_query_depth=0;std::uint64_t composition_scene_frame=0,frame=0,draws=0,id=1;std::shared_ptr<lattice_state::Capture> lattice_state;
    template<class F>F get(unsigned slot){return reinterpret_cast<F>(slot==1?reinterpret_cast<void*>(fixture_native_addref):slot==38?reinterpret_cast<void*>(saved_rt):reinterpret_cast<void*>(native_draw));}
} ctx;
void nested_present_on_native(){
    call_preserved([&]{
        if(destruction_mode==2)active->invalidate();
        check(active->publish(output_directory.c_str()),"native nested Present published refusal");
        ctx.lattice_state.reset();active.reset();destroyed_during_native=true;
        // The extracted production hook's local shared pin is now the sole
        // owner; its last Release occurs on the light draw path after result.
    });
}
struct Devices{std::shared_ptr<Device> value{&ctx,[](Device*){}};std::shared_ptr<Device> at(IDirect3DDevice9*){return value;}} devices;
enum class DrawMethod{Indexed};struct InputArgs{DrawMethod method;D3DPRIMITIVETYPE t;UINT c,s;INT b;UINT m,n;};
int read_draw_input(Device&,IDirect3DDevice9*,InputArgs){return 0;}
void snapshot(IDirect3DDevice9*,const char*,D3DPRIMITIVETYPE,UINT,bool,INT,UINT){++ctx.draws;}
void record_draw_input(Device&,int,HRESULT){}
#include "lattice_state_draw_under_test_inc.h"
void draw(unsigned slot){group=slot;seed(false);image(expected_input);const auto wanted=submit?backend_result:D3DERR_INVALIDCALL;
    const auto hr=draw_indexed(reinterpret_cast<IDirect3DDevice9*>(&device),D3DPT_TRIANGLELIST,0,0,slot?1267:9680,0,slot?940:3784);
    Image got{};image(got);check(hr==wanted,"native result unchanged");check(same(got,submit?expected_output:expected_input),"outgoing CPU/LastError preserved");}
int main(int argc,char** argv){
    if(argc!=4){std::fprintf(stderr,"usage: fixture captured-vs.bin captured-ps.bin output-directory\n");return 2;}
    if(!read_shader(argv[1],vertex_code)||!read_shader(argv[2],pixel_code))return 3;
    wchar_t path[1024]{};if(!MultiByteToWideChar(CP_UTF8,0,argv[3],-1,path,1024))return 4;output_directory=path;CreateDirectoryW(path,nullptr);
    initialize();ctx.caps.MaxStreams=2;ctx.caps.MaxUserClipPlanes=1;ctx.caps.MaxVertexShaderConst=256;ctx.caps.VertexShaderVersion=0xfffe0300;ctx.caps.PixelShaderVersion=0xffff0300;ctx.caps.NumSimultaneousRTs=4;
    for(unsigned scenario=0;scenario<12;++scenario){
        active=std::make_shared<lattice_state::Capture>();ctx.lattice_state=active;ctx.draws=0;ctx.frame=++frame;
        wrong_shader=wrong_declaration=fail_render=fail_stream=false;submit=true;backend_result=S_OK;nested_mode=0;nested_done=nested_publish=false;nested_original=0;
        destruction_mode=scenario>=10?scenario-9:0;destroyed_during_native=false;
        native_calls=saved_rt_calls=0;active->arm(1,frame,0,true);
        if(scenario==3)fail_render=true;
        if(scenario==4)fail_stream=true;
        if(scenario==5)submit=false;
        if(scenario==6)backend_result=D3DERR_INVALIDCALL;
        if(scenario==8||scenario==9)nested_mode=scenario==8?1:2;
        draw(0);
        if(scenario==1){wrong_shader=true;draw(0);wrong_shader=false;wrong_declaration=true;draw(0);wrong_declaration=false;}
        if(scenario==2)draw(0);
        if(scenario==7)active->invalidate();
        if(scenario!=5&&scenario!=6)draw(1);
        check(native_calls==(scenario==5?0u:scenario==6?1u:scenario==1?4u:scenario==2?3u:2u),"dispatch count");
        if(scenario==8)check(nested_done&&!nested_publish,"nested publication deferred");
        if(scenario==9)check(nested_done&&nested_original==-1,"nested selector refused");
        for(auto* r:{&vb,&ib,&vs,&ps,&bad_ps,&decl,&target,&depth,&texture})check(r->refs==1,"COM getter references balanced");
        check(!logical_rt_calls&&!forbidden_calls,"no logical getter/state-write/payload dispatch");
        if(active){
            seed(false);Image before{},after{};image(before);bool published=false;
            call_preserved([&]{published=active->publish(output_directory.c_str());});image(after);
            check(published&&same(before,after),"publish CPU envelope");
        }else check(destroyed_during_native,"last shared pin destruction edge exercised");
        if(scenario==0||scenario==1||scenario>=10)check(saved_rt_calls==8,"exact saved RT getter calls");
        std::printf("case=%u frame=%llu draws=%u saved_rt=%u log=%s\n",scenario,frame,native_calls,saved_rt_calls,logs.back().c_str());
        ctx.lattice_state.reset();active.reset();
    }
    std::printf("lattice_state_fixture checks=%u failures=%u logical_rt=%u forbidden=%u\n",checks,failures,logical_rt_calls,forbidden_calls);
    return failures?1:0;
}
