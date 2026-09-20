// Actual production gate on relocated synthetic helpers. Never launches game.
#include "media_presentation_gate_contract_fixture.h"
#include "../../src/proxy/media_presentation_gate_win32.h"
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstring>
#include <new>
namespace x3m::object_trace { bool executable_verified(){return true;} }
namespace x3m::engine_patch { bool install_window_open(){return true;} }
namespace gate=x3m::media_presentation_gate;
using gate_fixture::check;
extern "C" {
struct Snapshot { std::uint32_t regs[9];unsigned char xmm[128],x87[108];std::uint32_t mxcsr; };
Snapshot* fixture_output=nullptr;
std::uint32_t fixture_entry_esp=0,fixture_exit_esp=0;
alignas(16) unsigned char fixture_input_x87[108];
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw=0x077f;
std::uint32_t fixture_mxcsr=0x3f80;
std::uint32_t fixture_mxcsr_applied=0,fixture_mxcsr_input=0,fixture_mxcsr_output=0,fixture_mxcsr_after_x87=0;
std::uint32_t fixture_flags=0x647;
void fixture_call(std::uint32_t target,std::uint32_t ebx,std::uint32_t ebp,std::uint32_t eax,std::uint32_t ecx,std::uint32_t edx);
void fixture_nop_ret();void fixture_pop_ret();
}
// Four-byte caller stack, no 16-byte alignment promise; hostile live state:
// Three live x87 values, including a masked-invalid result, plus the st(0)
// value the engine may hold across a site;
// a non-default x87 control word and MXCSR, seeded XMM0-7, DF and the
// arithmetic flags set. The harness of game_phase_cpu_fixture.cpp.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
"movl %esp,_fixture_entry_esp\n"
"pushfl\n pushal\n subl $256,%esp\n"
"stmxcsr 108(%esp)\n fnsave 0(%esp)\n frstor 0(%esp)\n"
"movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
"movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
"movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
"fninit\n fldz\n fldz\n fdivp %st,%st(1)\n fld1\n fldpi\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n stmxcsr _fixture_mxcsr_applied\n"
"movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
"movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
"movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
"movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl _fixture_flags\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n stmxcsr _fixture_mxcsr_input\n call *240(%esp)\n stmxcsr _fixture_mxcsr_output\n"
"pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
"movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
"movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
"stmxcsr 272(%edi)\n fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr _fixture_mxcsr_after_x87\n addl $36,%esp\n"
"frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
"movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
"movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
"addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n"
// Stand-ins for the relocated call targets: D3DXMatrixInverse / _malloc (the
// body balances their arguments itself) and the queue walk 0x0047e6e0 (one
// pushed argument, popped here so the body stays linear).
".globl _fixture_nop_ret\n_fixture_nop_ret:\n ret\n"
".globl _fixture_pop_ret\n_fixture_pop_ret:\n ret $4\n");

extern "C" { std::uint32_t observed_entry_esp=0,observed_forward_esp=0; }
static gate::Win32Platform platform;
static std::uint32_t pointer(const void* p){return reinterpret_cast<std::uint32_t>(p);}
static void word(unsigned char* out,std::uint32_t value){std::memcpy(out,&value,4);}
static void jump(unsigned char* out,std::uint32_t here,std::uint32_t target){out[0]=0xe9;word(out+1,target-here-5);}
static bool seal(std::uint32_t code,unsigned size){std::uint32_t old=0;return platform.flush(code,size)&&platform.protect(code,size,gate::read_execute,old);}
struct Body {
    std::uint32_t code=0,bridge=0,root=0;
    gate::Site site{};
    std::uint32_t renderer[8]{},available=0,forwarded=0;
};
// The first 8 bytes and lost-return block have the exact proved opcodes;
// absolute addresses alone are relocated into fixture-owned memory. The
// continuation's MOV ECX reads its seeded value from the synthetic renderer.
static bool body(Body& b,void (*native)()=nullptr){
    b.code=platform.reserve();b.root=pointer(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!b.code||!b.root)return false;
    b.renderer[6]=0x56789abc;*reinterpret_cast<std::uint32_t*>(b.root)=pointer(b.renderer);
    b.site={b.code,b.root,b.code+5,b.code+128,pointer(&b.available),{}, {}};
    std::memcpy(b.site.entry_bytes,gate::game_site().entry_bytes,8);word(b.site.entry_bytes+1,b.root);
    std::memcpy(b.site.defer_bytes,gate::game_site().defer_bytes,11);word(b.site.defer_bytes+2,pointer(&b.available));
    unsigned char code[256]{};std::memcpy(code,b.site.entry_bytes,8);
    if(native){jump(code+8,b.code+8,pointer(reinterpret_cast<void*>(native)));}
    else {
        // MOV [forward ESP],ESP; MOV [forwarded],1; RET -- flags untouched.
        code[8]=0x89;code[9]=0x25;word(code+10,pointer(&observed_forward_esp));
        code[14]=0xc7;code[15]=0x05;word(code+16,pointer(&b.forwarded));word(code+20,1);code[24]=0xc3;
    }
    std::memcpy(code+128,b.site.defer_bytes,11);
    b.bridge=b.code+160;code[160]=0x89;code[161]=0x25;word(code+162,pointer(&observed_entry_esp));
    jump(code+166,b.code+166,b.code);
    return platform.write_reserved(b.code,code,sizeof code)&&seal(b.code,sizeof code);
}
static std::uint32_t caller(std::uint32_t target,unsigned padding){
    const auto at=platform.reserve();if(!at)return 0;
    unsigned char code[16]={0x8d,0x64,0x24,static_cast<unsigned char>(0u-padding),0xe8,0,0,0,0,
        0x8d,0x64,0x24,static_cast<unsigned char>(padding),0xc3};
    word(code+5,target-(at+9));
    return platform.write_reserved(at,code,sizeof code)&&seal(at,sizeof code)?at:0;
}
static unsigned mxcsr_calls=0,mxcsr_requested_status_observed=0,mxcsr_unrepresented_status=0;
static void invoke(std::uint32_t target,Snapshot& out){
    fixture_output=&out;SetLastError(0x13572468);
    fixture_call(target,0x23456789,0x3456789a,0x456789ab,0x56789abc,0x6789abcd);
    check(GetLastError()==0x13572468,"gate leaves LastError unchanged");
    check(fixture_entry_esp==fixture_exit_esp,"caller original ESP restored");
    check(!std::memcmp(fixture_input_x87,out.x87,108),"full x87 image survives gate, live stack/status/control");
    check(!std::memcmp(fixture_xmm_seed,out.xmm,128),"all seeded XMM registers survive");
    // Requested bits are not necessarily materialized by an emulator. Existing
    // FEX measurements record LDMXCSR 0x3fbf -> STMXCSR 0x3f80; the current
    // 0x3fa5 seed must therefore be independently observed before gate entry.
    // NO status mask is used for preservation: compare all 32 actual bits.
    check(fixture_mxcsr_applied==fixture_mxcsr||fixture_mxcsr_applied==0x3f80,
          "MXCSR seed roundtrip exact or known unrepresented FEX sticky status");
    check(fixture_mxcsr_input==fixture_mxcsr_applied,"pre-call harness preserves every applied MXCSR bit");
    check(fixture_mxcsr_output==fixture_mxcsr_input,"immediate return preserves full MXCSR including actual status bits");
    check(out.mxcsr==fixture_mxcsr_input,"MXCSR preserved including status bits");
    ++mxcsr_calls;
    if(fixture_mxcsr_applied==fixture_mxcsr)++mxcsr_requested_status_observed;
    else ++mxcsr_unrepresented_status;
    std::printf("MXCSR_BOUNDARY call=%u requested=%08lx applied=%08lx entry=%08lx return=%08lx snapshot=%08lx after_x87=%08lx requested_status_observed=%u\n",
        mxcsr_calls,static_cast<unsigned long>(fixture_mxcsr),static_cast<unsigned long>(fixture_mxcsr_applied),
        static_cast<unsigned long>(fixture_mxcsr_input),static_cast<unsigned long>(fixture_mxcsr_output),
        static_cast<unsigned long>(out.mxcsr),static_cast<unsigned long>(fixture_mxcsr_after_x87),
        unsigned(fixture_mxcsr_applied==fixture_mxcsr));
}
static void compare(const Snapshot& native,const Snapshot& gated){
    for(unsigned i=0;i<9;++i)if(i!=3)check(native.regs[i]==gated.regs[i],"GPR and flags match unmodified route");
    check(!std::memcmp(native.xmm,gated.xmm,128)&&!std::memcmp(native.x87,gated.x87,108)&&native.mxcsr==gated.mxcsr,"entire FP envelope matches baseline");
}
struct Cleanup {gate::Admission* admission;Body* body;unsigned calls=0;};
static void finish(void* context) noexcept {
    auto& c=*static_cast<Cleanup*>(context);++c.calls;
    check(c.admission->depth()==1,"scope active on final cleanup entry");
    Snapshot out{};c.body->available=1;c.body->forwarded=0;
    invoke(c.body->bridge,out);
    check(c.body->available==0&&c.body->forwarded==0,"nested presentation during final cleanup defers");
    check(c.admission->depth()==1,"scope remains active after final cleanup work");
}
static void cpu(){
    Body b;const bool made=body(b);check(made,"relocated helper emitted");if(!made)return;
    auto* runtime=gate::process_runtime();check(runtime!=nullptr,"process-lifetime runtime allocated");if(!runtime)return;
    auto& a=runtime->admission;auto& t=runtime->transaction;
    const auto tid=GetCurrentThreadId();check(a.qualify_owner(tid),"CPU fixture owner");
    std::uint32_t callers[4]{},lost_callers[4]{};Snapshot baseline[4]{},lost[4]{};
    bool residues[4]{};
    for(unsigned i=0;i<4;++i){
        callers[i]=caller(b.bridge,i*4);lost_callers[i]=caller(b.site.defer,i*4);
        check(callers[i]&&lost_callers[i],"alignment caller emitted");if(!callers[i]||!lost_callers[i])return;
        invoke(callers[i],baseline[i]);residues[(observed_entry_esp&15)/4]=true;
        check(observed_entry_esp==observed_forward_esp,"forward reaches continuation at original ESP");
        invoke(lost_callers[i],lost[i]);
    }
    check(residues[0]&&residues[1]&&residues[2]&&residues[3],"all four incoming ESP residues exercised");
    const bool installed=gate::stage(platform,t,b.site,a)&&gate::install(platform,t,a);
    check(installed,"actual Win32 stage/install");if(!installed)return;
    check(!a.enabled(),"install never enables copies");
    for(unsigned i=0;i<4;++i){Snapshot out{};invoke(callers[i],out);compare(baseline[i],out);}
    check(a.enable(tid),"explicit fixture admission enable");Cleanup c{&a,&b};
    {gate::CopyScope copy(a,tid,finish,&c);check(bool(copy),"one active copy scope");
     gate::CopyScope nested(a,tid,finish,&c);check(!nested&&a.depth()==1,"second active copy rejected");
     std::uint32_t old=0;check(platform.protect(b.root,4,PAGE_NOACCESS,old),"renderer root deliberately inaccessible");
     for(unsigned i=0;i<4;++i){Snapshot out{};b.available=1;b.forwarded=0;invoke(callers[i],out);compare(lost[i],out);
         check(b.available==0&&b.forwarded==0,"defer returns exactly once without renderer read or native path");}
     check(!gate::restore(platform,t,a,true)&&t.may_redirect,"active lease prevents detachment");
     std::uint32_t ignored=0;check(platform.protect(b.root,4,old,ignored),"renderer root accessible again");
    }
    check(!a.depth()&&c.calls==1,"cleanup releases active scope exactly once");
    for(unsigned i=0;i<4;++i){Snapshot out{};b.forwarded=0;invoke(callers[i],out);compare(baseline[i],out);check(b.forwarded==1,"ordinary forwarding resumes after drop");}
    check(gate::restore(platform,t,a,true),"actual Win32 restore");unsigned char bytes[8]{};
    check(platform.read(b.site.entry,bytes,8)&&!std::memcmp(bytes,b.site.entry_bytes,8),"restore exact eight bytes including neighbors");
    MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(b.code),&info,sizeof info);
    check(info.Protect==PAGE_EXECUTE_READ,"actual site RX protection restored");
    // Ordinary calls through a retired gate remain valid for previously fetched
    // redirects. The counter is process-lifetime, so this must still forward.
    Snapshot retired{};invoke(t.code,retired);check(b.forwarded==1,"retired emitted gate remains executable");
}

// Synthetic engine path + actual documented native D3D9 default-pool lease.
// This does not qualify engine binding observers or canonical wrapper lookup.
static IDirect3DDevice9* device=nullptr;
static D3DPRESENT_PARAMETERS parameters{};
static gate::Admission* reset_admission=nullptr;
static unsigned native_calls=0,present_calls=0,test_calls=0,reset_calls=0;
static HRESULT reset_result=E_FAIL;
static void native_chain(){
    ++native_calls;const auto tid=GetCurrentThreadId();
    const bool engine=reset_admission->begin_reset(tid,gate::Admission::Reset::engine);
    check(engine,"whole synthetic engine Reset entry");if(!engine)return;
    ++present_calls;device->Present(nullptr,nullptr,nullptr,nullptr);
    ++test_calls;device->TestCooperativeLevel();
    const bool native=reset_admission->begin_reset(tid,gate::Admission::Reset::native);
    check(native,"native Reset admission after cleanup");if(!native)return;
    ++reset_calls;reset_result=device->Reset(&parameters);
    check(reset_admission->end_reset(tid,gate::Admission::Reset::native,SUCCEEDED(reset_result)),"native result observed");
    check(reset_admission->end_reset(tid,gate::Admission::Reset::engine,SUCCEEDED(reset_result)),"engine reset exit observed");
}
struct SurfaceCleanup {gate::Admission* a;Body* body;IDirect3DSurface9* surface;unsigned calls=0;};
static void release_surface(void* context) noexcept {
    auto& c=*static_cast<SurfaceCleanup*>(context);++c.calls;
    check(c.a->depth()==1,"real surface Release starts with active copy scope");
    if(c.surface){c.surface->Release();c.surface=nullptr;}
    // Reentry immediately AFTER the last actual Release still sees depth one.
    reinterpret_cast<void(*)()>(c.body->code)();
    check(c.a->depth()==1&&native_calls==0,"depth covers final surface Release and subsequent nested presentation");
}
static void d3d(){
    HWND window=CreateWindowA("STATIC","media gate fixture",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    check(window!=nullptr,"fixture window created");if(!window)return;
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);check(d3d!=nullptr,"native D3D9 available");if(!d3d){DestroyWindow(window);return;}
    parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;parameters.hDeviceWindow=window;
    parameters.BackBufferWidth=64;parameters.BackBufferHeight=64;parameters.BackBufferFormat=D3DFMT_UNKNOWN;
    const HRESULT create=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&parameters,&device);
    check(SUCCEEDED(create),"native D3D9 fixture device created");if(FAILED(create)){d3d->Release();DestroyWindow(window);return;}
    // Process lifetime admission storage for a second synthetic helper, matching
    // the production runtime lifetime contract without reusing its transaction.
    void* storage=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    check(storage!=nullptr,"D3D admission storage allocated");
    if(!storage){device->Release();d3d->Release();DestroyWindow(window);return;}
    auto& a=*new(storage)gate::Admission;reset_admission=&a;
    Body b;const bool made=body(b,native_chain);check(made,"D3D synthetic helper emitted");
    if(!made){device->Release();d3d->Release();DestroyWindow(window);return;}
    gate::Transaction t;
    check(a.qualify_owner(GetCurrentThreadId()),"D3D owner qualified");
    const bool installed=gate::stage(platform,t,b.site,a)&&gate::install(platform,t,a);
    check(installed,"D3D helper gate installed");
    if(installed){
        check(a.enable(GetCurrentThreadId()),"D3D synthetic admission enabled");
        IDirect3DSurface9* surface=nullptr;
        // No new default-pool reference exists before the scope becomes active.
        SurfaceCleanup cleanup{&a,&b,nullptr};
        {gate::CopyScope copy(a,GetCurrentThreadId(),release_surface,&cleanup);check(bool(copy),"D3D copy scope active before acquire");
         const HRESULT hr=device->CreateOffscreenPlainSurface(8,8,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&surface,nullptr);
         check(SUCCEEDED(hr),"actual DEFAULT-pool surface acquired");cleanup.surface=surface;
         if(surface){
             D3DSURFACE_DESC desc{};check(SUCCEEDED(surface->GetDesc(&desc))&&desc.Pool==D3DPOOL_DEFAULT,"public pool observed");
             D3DLOCKED_RECT locked{};const HRESULT lock=surface->LockRect(&locked,nullptr,0);
             check(SUCCEEDED(lock),"default surface lock acquired");
             if(SUCCEEDED(lock)){
                 std::memset(locked.pBits,0x5a,8*4);
                 reinterpret_cast<void(*)()>(b.code)();
                 check(native_calls==0&&reset_calls==0&&b.available==0,"mapped DEFAULT lease suppresses native Present/Test/Reset");
                 check(SUCCEEDED(surface->UnlockRect()),"surface unlocked while depth active");
             }
         }
        }
        check(!a.depth()&&cleanup.calls==1&&cleanup.surface==nullptr,"surface final Release before depth zero");
        reinterpret_cast<void(*)()>(b.code)();
        check(native_calls==1&&present_calls==1&&test_calls==1&&reset_calls==1&&SUCCEEDED(reset_result),"ordinary native Reset succeeds after lease cleanup");
        check(gate::restore(platform,t,a,true),"D3D gate restored");
    }
    device->Release();device=nullptr;d3d->Release();DestroyWindow(window);
}
int main(){
    for(unsigned i=0;i<128;++i)fixture_xmm_seed[i]=static_cast<unsigned char>(0xa5^i*7);
    fixture_mxcsr=0x3fa5; // masked exception-status bits + non-default rounding
    gate_fixture::run();cpu();d3d();
    std::printf("MXCSR_COVERAGE calls=%u requested_status_observed=%u unrepresented_status=%u preservation_comparison=all_32_bits hostile_nonzero_status=%s\n",
        mxcsr_calls,mxcsr_requested_status_observed,mxcsr_unrepresented_status,
        mxcsr_unrepresented_status?"UNVERIFIED_ON_THIS_RUNTIME":"observed");
    std::printf("MEDIA GATE X86 checks=%u failures=%u native_present=%u native_test=%u native_reset=%u reset_hr=%08lx\n",
        gate_fixture::checks,gate_fixture::failures,present_calls,test_calls,reset_calls,static_cast<unsigned long>(reset_result));
    return gate_fixture::failures?1:0;
}
