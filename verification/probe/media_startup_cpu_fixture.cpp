// Actual shared export assembly and production Windows startup adapter.
// Authored primary caller/anchors; backend factory and service are test doubles.
#include "../../src/proxy/media_startup.h"
#include "../../src/proxy/media_startup_abi.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <atomic>
#include <initializer_list>
namespace startup=x3m::media_startup;
static unsigned checks=0,failures=0;
static void check(bool okay,const char* name){++checks;if(!okay){++failures;std::printf("FAIL %s\n",name);}}
static bool image_identity=true;
namespace x3m::object_trace {bool executable_verified(){return image_identity;}}
extern "C" std::uintptr_t WINAPI Direct3DCreate9(UINT);
X3M_MEDIA_STARTUP_EXPORT("_Direct3DCreate9@4","_fixture_factory@4")
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

extern "C" {
Snapshot delegate_input{};
std::uint32_t delegate_sdk=0,delegate_entry_esp=0,factory_result=0x13579bdf;
std::uint16_t output_cw=0x0b7f;
std::uint32_t output_mxcsr=0x5fa5,output_flags=0x247;
alignas(16) unsigned char output_xmm[128];
void __cdecl native_control();
std::uintptr_t WINAPI fixture_factory(UINT);
void __cdecl x3m_media_startup_enter(startup::Entry*,std::uintptr_t) noexcept;
void __cdecl x3m_media_startup_leave(startup::Entry*,std::uintptr_t) noexcept;
}
// Capture the delegate's ACTUAL input before changing it. Its intentional output
// differs from the caller seed; the new boundary must preserve that output.
asm(".text\n.globl _fixture_factory@4\n_fixture_factory@4:\n"
"movl %esp,_delegate_entry_esp\n pushfl\n pushal\n"
"movl %esp,%esi\n movl $_delegate_input,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
"movl $_delegate_input,%edi\n stmxcsr 272(%edi)\n"
"movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
"movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
"fnsave 164(%edi)\n movl 40(%esp),%eax\n movl %eax,_delegate_sdk\n"
"ldmxcsr _x3m_media_startup_default_mxcsr\n call _native_control\n popal\n popfl\n"
"fninit\n fld1\n fldln2\n fldpi\n fldcw _output_cw\n ldmxcsr _output_mxcsr\n"
"movups _output_xmm,%xmm0\n movups _output_xmm+16,%xmm1\n movups _output_xmm+32,%xmm2\n movups _output_xmm+48,%xmm3\n"
"movups _output_xmm+64,%xmm4\n movups _output_xmm+80,%xmm5\n movups _output_xmm+96,%xmm6\n movups _output_xmm+112,%xmm7\n"
"movl _factory_result,%eax\n movl $0xabc12345,%ecx\n movl $0xdef67890,%edx\n pushl _output_flags\n popfl\n ret $4\n");
static const char* mode="success";
static bool actions=false,nested=false;
static DWORD main_thread=0;
static std::atomic<unsigned> callbacks{0};
static std::atomic<bool> callback_thread_ok{false},callback_module_ok{false};
static void word(unsigned char* p,std::uint32_t value){std::memcpy(p,&value,4);}
static std::uint32_t address(const void* p){return reinterpret_cast<std::uint32_t>(p);}
extern "C" void __cdecl native_control(){
    if(actions&&!nested){
        if(!std::strcmp(mode,"reentrant")){nested=true;Direct3DCreate9(0x20);nested=false;}
        if(!std::strcmp(mode,"device_inside"))x3m_media_startup_device_attempt();
        if(!std::strcmp(mode,"anchor"))*reinterpret_cast<unsigned char*>(0x402edc)=0x90;
    }
    SetLastError(0x24681357);
}
static bool bootstrap(void*,const startup::BootstrapContext& context) noexcept {
    ++callbacks;
    callback_thread_ok=GetCurrentThreadId()!=main_thread;
    callback_module_ok=context.pinned_proxy==GetModuleHandleW(nullptr)&&context.requested_qpc!=0;
    return std::strcmp(mode,"callbackfail")!=0;
}
static unsigned installs=0;
static bool install_thread_ok=false;
static bool install_hooks(void*) noexcept {
    ++installs;
    install_thread_ok=GetCurrentThreadId()==main_thread&&startup::snapshot().entry_active&&
        !startup::snapshot().request_claimed&&!callbacks.load();
    if(!std::strcmp(mode,"installdevice"))x3m_media_startup_device_attempt();
    // Deliberate callback effects must not escape the existing leave envelope.
    SetLastError(0xdeadbeef);
    asm volatile("fninit\n fld1\n xorps %%xmm0,%%xmm0":::"xmm0","memory");
    return std::strcmp(mode,"installfail")!=0;
}
// Established fixture pattern: PE loader reserves authored game addresses
// before CRT/heap mappings. Real fixture sections are isolated at >=0x630000.
// No fixed VirtualAlloc request and no overwrite of another mapping is used.
__attribute__((section(".x3map"),used)) unsigned char fixture_map[0x21f000]{};
static bool owned_map(){
    const auto module=GetModuleHandleW(nullptr);
    std::printf("STARTUP_MAP strategy=owned_pe_section module=%08lx map=%08lx bytes=%08lx real_code=%08lx\n",
        static_cast<unsigned long>(address(module)),static_cast<unsigned long>(address(fixture_map)),
        static_cast<unsigned long>(sizeof fixture_map),static_cast<unsigned long>(address(reinterpret_cast<void*>(&fixture_factory))));
    if(address(module)!=0x400000||address(fixture_map)!=0x401000||address(reinterpret_cast<void*>(&fixture_factory))<0x630000)return false;
    struct Span {std::uint32_t address;unsigned size;};
    const Span spans[]={{0x402edc,6},{0x4d8470,64},{0x4faedc,6},{0x532314,4},{0x608b3c,4}};
    for(const auto span:spans){
        MEMORY_BASIC_INFORMATION info{};SetLastError(0);
        const auto queried=VirtualQuery(reinterpret_cast<void*>(span.address),&info,sizeof info);
        const DWORD query_error=GetLastError();
        const bool owned=queried==sizeof info&&info.AllocationBase==module&&info.Type==MEM_IMAGE&&info.State==MEM_COMMIT&&
            span.address>=address(fixture_map)&&span.address+span.size<=address(fixture_map)+sizeof fixture_map&&
            span.address+span.size<=address(info.BaseAddress)+info.RegionSize;
        std::printf("STARTUP_MAP span=%08lx bytes=%u query=%lu error=%lu allocation=%08lx state=%08lx protect=%08lx type=%08lx owned=%u\n",
            static_cast<unsigned long>(span.address),span.size,static_cast<unsigned long>(queried),static_cast<unsigned long>(query_error),
            static_cast<unsigned long>(address(info.AllocationBase)),static_cast<unsigned long>(info.State),
            static_cast<unsigned long>(info.Protect),static_cast<unsigned long>(info.Type),unsigned(owned));
        if(!owned)return false;
        DWORD old=0;SetLastError(0);
        const BOOL writable=VirtualProtect(reinterpret_cast<void*>(span.address),span.size,PAGE_EXECUTE_READWRITE,&old);
        const DWORD protect_error=GetLastError();
        if(!writable){std::printf("STARTUP_MAP_PROTECT span=%08lx error=%lu\n",static_cast<unsigned long>(span.address),static_cast<unsigned long>(protect_error));return false;}
    }
    return true;
}
static bool primary(){
    if(!owned_map())return false;
    auto* entry=reinterpret_cast<unsigned char*>(0x402edc);
    entry[0]=0xe8;word(entry+1,0x4d8470-(0x402edc+5));entry[5]=0xc3;
    auto* setup=reinterpret_cast<unsigned char*>(0x4d8470);std::memset(setup,0x90,64);
    setup[0]=0x53;setup[1]=0x56;setup[2]=0x57;
    setup[0x18]=0x6a;setup[0x19]=0x20;
    setup[0x1f]=0xe8;word(setup+0x20,0x4faedc-(0x4d848f+5));
    const unsigned char after[]={0x8b,0x0d,0x3c,0x8b,0x60,0,0x5f,0x5e,0x5b,0xc3};
    std::memcpy(setup+0x24,after,sizeof after);
    const unsigned char thunk[]={0xff,0x25,0x14,0x23,0x53,0};std::memcpy(reinterpret_cast<void*>(0x4faedc),thunk,6);
    *reinterpret_cast<std::uint32_t*>(0x608b3c)=0xabc12345;
    SetLastError(0);const BOOL flushed=FlushInstructionCache(GetCurrentProcess(),fixture_map,sizeof fixture_map);
    const DWORD error=GetLastError();
    std::printf("STARTUP_MAP_FLUSH okay=%u error=%lu\n",unsigned(flushed!=FALSE),static_cast<unsigned long>(error));
    return flushed!=FALSE;
}
static std::uint32_t caller(std::uint32_t target,unsigned padding,bool sdk){
    auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!p)return 0;
    unsigned n=0;
    p[n++]=0x8d;p[n++]=0x64;p[n++]=0x24;p[n++]=static_cast<unsigned char>(0u-padding);
    if(sdk){p[n++]=0x6a;p[n++]=0x20;}
    p[n++]=0xe8;word(p+n,target-(address(p)+n+4));n+=4;
    p[n++]=0x8d;p[n++]=0x64;p[n++]=0x24;p[n++]=static_cast<unsigned char>(padding);p[n++]=0xc3;
    DWORD old=0;
    return FlushInstructionCache(GetCurrentProcess(),p,n)&&VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old)?address(p):0;
}
static void invoke(std::uint32_t target,Snapshot& out,bool native=true){
    fixture_output=&out;SetLastError(0x13572468);
    fixture_call(target,0x23456789,0x3456789a,0x456789ab,0x56789abc,0x6789abcd);
    check(fixture_entry_esp==fixture_exit_esp,"original caller stack restored");
    check(GetLastError()==(native?0x24681357u:0x13572468u),"actual original-output LastError survives new work");
}
static void equal(const Snapshot& a,const Snapshot& b,const char* label){
    bool registers=true;for(unsigned i=0;i<9;++i)if(i!=3)registers=registers&&a.regs[i]==b.regs[i];
    check(registers,label);check(!std::memcmp(a.x87,b.x87,108),"full represented x87 image matches original");
    check(!std::memcmp(a.xmm,b.xmm,128),"all XMM0-7 match original");check(a.mxcsr==b.mxcsr,"all 32 represented MXCSR bits match original");
}
static void reads(){
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(memory!=nullptr,"safe-read fixture allocation");if(!memory)return;
    DWORD old=0;check(VirtualProtect(memory+4096,4096,PAGE_NOACCESS,&old)!=FALSE,"safe-read inaccessible page");
    startup::Entry entry;SetLastError(0x11223344);
    x3m_media_startup_enter(&entry,address(memory+4096-12));
    check(!entry.matched&&entry.owns_entry&&GetLastError()==0x11223344,"actual cross-page safe-read rejects unreadable ancestor without fault");
    x3m_media_startup_leave(&entry,1);
    check(!startup::snapshot().entry_active&&!callbacks,"unreadable entry forwards without retained context");
    VirtualFree(memory,0,MEM_RELEASE);
}
int main(int argc,char** argv){
    if(argc>1)mode=argv[1];
    main_thread=GetCurrentThreadId();
    for(unsigned i=0;i<128;++i){fixture_xmm_seed[i]=static_cast<unsigned char>(0xa5^i*7);output_xmm[i]=static_cast<unsigned char>(0x39^i*11);}
    fixture_mxcsr=0x3fa5;
    check(primary(),"authored ordinary caller and exact anchor opcodes mapped");if(failures)return 1;
    reads();
    check(startup::configure(bootstrap,nullptr,install_hooks),"production one-shot callbacks registered");
    const bool unknown=!std::strcmp(mode,"unknown");
    const bool want_callback=!std::strcmp(mode,"success")||!std::strcmp(mode,"callbackfail");
    const bool want_install=want_callback||!std::strcmp(mode,"installfail")||!std::strcmp(mode,"installdevice");
    Snapshot baseline[4]{},inputs[4]{};std::uint32_t callers[4]{},direct[4]{};bool residues[4]{};
    for(unsigned i=0;i<4;++i){
        callers[i]=caller(unknown?address(reinterpret_cast<void*>(&Direct3DCreate9)):0x402edc,i*4,unknown);
        direct[i]=unknown?caller(address(reinterpret_cast<void*>(&fixture_factory)),i*4,true):callers[i];
        check(callers[i]&&direct[i],"four-byte alignment callers emitted");if(failures)return 1;
    }
    if(!std::strcmp(mode,"failed"))factory_result=0;
    *reinterpret_cast<std::uint32_t*>(0x532314)=address(reinterpret_cast<void*>(&fixture_factory));
    for(unsigned i=0;i<4;++i){invoke(direct[i],baseline[i]);inputs[i]=delegate_input;residues[(delegate_entry_esp&15)/4]=true;}
    check(residues[0]&&residues[1]&&residues[2]&&residues[3],"all four delegate incoming stack residues");
    *reinterpret_cast<std::uint32_t*>(0x532314)=address(reinterpret_cast<void*>(&Direct3DCreate9));
    if(!std::strcmp(mode,"identity"))image_identity=false;
    if(!std::strcmp(mode,"late"))x3m_media_startup_device_attempt();
    actions=true;
    for(unsigned i=0;i<4;++i){
        // Restore the authored setup call after each deliberate anchor rejection.
        *reinterpret_cast<unsigned char*>(0x402edc)=0xe8;
        Snapshot out{};invoke(callers[i],out);equal(baseline[i],out,"all original output GPRs/flags preserved");
        if(std::strcmp(mode,"reentrant"))equal(inputs[i],delegate_input,"all original input GPRs/flags preserved");
        check(delegate_sdk==0x20,"delegate receives original SDK exactly");
    }
    const DWORD start=GetTickCount();
    if(want_callback){while(GetTickCount()-start<5000){const auto state=startup::snapshot().status;
        if(state==startup::Status::prepared||state==startup::Status::callback_failed)break;
        Sleep(1);}}
    auto snapshot=startup::snapshot();
    check(installs==unsigned(want_install),"only qualified context invokes inert installer once");
    if(want_install)check(install_thread_ok,"installer runs synchronously before request publication");
    if(!std::strcmp(mode,"installfail"))check(snapshot.status==startup::Status::install_failed,"installer failure is permanent");
    check(callbacks.load()==unsigned(want_callback),"only one successful ordinary context schedules bootstrap");
    if(want_callback){
        check(callback_thread_ok&&callback_module_ok,"bootstrap runs on different thread with retained module");
        check(snapshot.requested_qpc&&snapshot.bootstrap_begin_qpc>=snapshot.requested_qpc&&snapshot.bootstrap_end_qpc>=snapshot.bootstrap_begin_qpc,"separate request/bootstrap start/end timestamps");
        if(!std::strcmp(mode,"success")){check(startup::service_ready(),"canonical service explicitly signals readiness");
            check(startup::snapshot().ready_qpc>=snapshot.bootstrap_end_qpc,"actual readiness timestamp is separate");}
        else check(snapshot.status==startup::Status::callback_failed&&!startup::service_ready(),"callback failure cannot claim ready");
    }else check(!snapshot.request_claimed&&!snapshot.requested_qpc,"rejected context has no startup request");
    // First-CreateDevice notification preserves the entire incoming state too.
    Snapshot untouched{},notified{};invoke(address(reinterpret_cast<void*>(&fixture_nop_ret)),untouched,false);
    invoke(address(reinterpret_cast<void*>(&x3m_media_startup_device_attempt)),notified,false);equal(untouched,notified,"CreateDevice notification preserves GPRs/flags");
    check(startup::snapshot().closed&&startup::snapshot().first_device_attempt_qpc,"first-device timestamp and private closure published");
    snapshot=startup::snapshot();
    std::printf("MEDIA STARTUP X86 mode=%s checks=%u failures=%u callbacks=%u installs=%u status=%u requested=%llu bootstrap_begin=%llu bootstrap_end=%llu ready=%llu device=%llu mxcsr_requested=%08lx mxcsr_applied=%08lx hostile_sticky_status=%s\n",
        mode,checks,failures,callbacks.load(),installs,unsigned(snapshot.status),snapshot.requested_qpc,snapshot.bootstrap_begin_qpc,snapshot.bootstrap_end_qpc,snapshot.ready_qpc,snapshot.first_device_attempt_qpc,
        static_cast<unsigned long>(fixture_mxcsr),static_cast<unsigned long>(fixture_mxcsr_applied),fixture_mxcsr_applied==fixture_mxcsr?"represented":"UNVERIFIED_UNREPRESENTED");
    return failures?1:0;
}
