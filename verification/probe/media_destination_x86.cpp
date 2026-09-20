// Production 19-site emitter + real Observer on synthetic engine storage.
// Ordinary CPU/LastError witness, four incoming stack alignments, both branches.
#include "../../src/proxy/media_destination.h"
#include "../../src/proxy/cpu_state.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace g=x3m::media_presentation_gate;
using namespace x3m::media_destination;
extern "C" {
std::uint32_t saved_stack,entry_stack,entry_target;
std::uint32_t input_gpr[8],output_gpr[8],input_flags=0x647,output_flags;
alignas(16) unsigned char input_xmm[128],output_xmm[128],input_x87[108],output_x87[108];
std::uint32_t input_mxcsr=0x3f80,output_mxcsr;
__attribute__((naked)) void capture_exit(){asm volatile(
 "mov %edi,_output_gpr\n mov %esi,_output_gpr+4\n mov %ebp,_output_gpr+8\n mov %esp,_output_gpr+12\n"
 "mov %ebx,_output_gpr+16\n mov %edx,_output_gpr+20\n mov %ecx,_output_gpr+24\n mov %eax,_output_gpr+28\n"
 "pushfl\n popl _output_flags\n"
 "movups %xmm0,_output_xmm\n movups %xmm1,_output_xmm+16\n movups %xmm2,_output_xmm+32\n movups %xmm3,_output_xmm+48\n"
 "movups %xmm4,_output_xmm+64\n movups %xmm5,_output_xmm+80\n movups %xmm6,_output_xmm+96\n movups %xmm7,_output_xmm+112\n"
 "fnsave _output_x87\n stmxcsr _output_mxcsr\n cld\n mov _saved_stack,%esp\n popal\n popfl\n ret\n");}
__attribute__((naked)) void enter_case(){asm volatile(
 "pushfl\n pushal\n mov %esp,_saved_stack\n mov _entry_stack,%esp\n"
 "fninit\n fld1\n fldz\n fnsave _input_x87\n frstor _input_x87\n ldmxcsr _input_mxcsr\n"
 "movups _input_xmm,%xmm0\n movups _input_xmm+16,%xmm1\n movups _input_xmm+32,%xmm2\n movups _input_xmm+48,%xmm3\n"
 "movups _input_xmm+64,%xmm4\n movups _input_xmm+80,%xmm5\n movups _input_xmm+96,%xmm6\n movups _input_xmm+112,%xmm7\n"
 "pushl _input_flags\n popfl\n mov _input_gpr,%edi\n mov _input_gpr+4,%esi\n mov _input_gpr+8,%ebp\n"
 "mov _input_gpr+16,%ebx\n mov _input_gpr+20,%edx\n mov _input_gpr+24,%ecx\n mov _input_gpr+28,%eax\n"
 "jmp *_entry_target\n");}
}
namespace {
unsigned checks=0,failures=0,case_id=0,calls=0;
Observer* actual=nullptr;
void check(bool v,const char* label){++checks;if(!v){++failures;std::printf("ABI FAIL case=%u %s\n",case_id,label);}}
unsigned ptr(const void* p){return reinterpret_cast<unsigned>(p);}
void put(unsigned at,unsigned value){std::memcpy(reinterpret_cast<void*>(at),&value,4);}
void bytes(unsigned at,std::initializer_list<unsigned char> value){std::memcpy(reinterpret_cast<void*>(at),value.begin(),value.size());}
struct Source:IdentitySource {bool snapshot(unsigned d,unsigned s,x3m::ownership::SurfaceLeaseIdentity& out) noexcept override {if(!d||!s)return false;out={1,1,s};return true;}};
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl abi_dispatch(Frame* frame,unsigned site) noexcept {
    x3m::PreserveCpuState cpu;++calls;
    // Deliberately hostile observer bookkeeping inside the exact emitter shell.
    SetLastError(0xdeadbeef);asm volatile("fninit\n fld1\n fld1\n xorps %%xmm0,%%xmm0\n xorps %%xmm1,%%xmm1\n xorps %%xmm2,%%xmm2\n xorps %%xmm3,%%xmm3\n xorps %%xmm4,%%xmm4\n xorps %%xmm5,%%xmm5\n xorps %%xmm6,%%xmm6\n xorps %%xmm7,%%xmm7":::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
    unsigned mx=0x1f80;asm volatile("ldmxcsr %0"::"m"(mx):"memory");actual->dispatch(site,*frame,GetCurrentThreadId());
}
struct Output {unsigned regs[8],flags,error,mxcsr;unsigned char xmm[128],x87[108];};
Output capture(){Output o;std::memcpy(o.regs,output_gpr,sizeof o.regs);o.flags=output_flags;o.error=GetLastError();o.mxcsr=output_mxcsr;std::memcpy(o.xmm,output_xmm,128);std::memcpy(o.x87,output_x87,108);return o;}
void initialize(unsigned stack,unsigned index,bool branch){
    std::memset(reinterpret_cast<void*>(0x600000),0,0x20000);
    put(0x6069ac,0x610000);put(0x6069b0,4);put(0x6069b4,0);put(0x608b2c,0x500);
    put(0x610008,0x610200);put(0x610108,0x610200);put(0x610204,0x610300);put(0x610230,0x400);
    put(0x610210,branch?0x610200:0); // MOVI compare input
    for(unsigned i=0;i<8;++i)input_gpr[i]=0x11110000+i*0x111;
    input_gpr[0]=0x610200;input_gpr[1]=0x610200;input_gpr[2]=0x12345678;input_gpr[3]=stack;
    input_gpr[4]=0;input_gpr[5]=0;input_gpr[6]=0x610000;input_gpr[7]=0x610200;
    std::memset(reinterpret_cast<void*>(stack-2048),0x55,4096);
    put(stack,ptr(reinterpret_cast<void*>(&capture_exit)));put(stack+4,0x610008);put(stack+8,8);put(stack+20,0x55558888);
    const auto id=static_cast<SiteId>(index);
    if(id==SiteId::slot_name_replace)input_gpr[7]=branch?0xffff:1;
    if(id==SiteId::wrapper_cleanup&&!branch)input_gpr[1]=0;
    if(id==SiteId::slot_publish_new)put(stack+12,ptr(reinterpret_cast<void*>(&capture_exit)));
    if(id==SiteId::reset_root_surface_publications){input_gpr[6]=0x610200;input_gpr[7]=0x610200;input_gpr[5]=0x400;}
    if(id==SiteId::record_bind_matched||id==SiteId::record_unbind_matched||id==SiteId::record_inline_bind||id==SiteId::restore_matched_search)input_gpr[6]=7;
    for(unsigned i=0;i<128;++i)input_xmm[i]=static_cast<unsigned char>(i*7+3);
    entry_stack=stack;calls=0;SetLastError(0x12345678);
}
void tails(unsigned index){
    const auto& s=sites[index];std::memcpy(reinterpret_cast<void*>(s.address),s.bytes,s.length);
    const auto id=static_cast<SiteId>(index);
    switch(id){
    case SiteId::table_initial_loader:bytes(s.continuation,{0x8d,0x64,0x24,8,0xc3});break;
    case SiteId::table_destroy:bytes(s.continuation,{0x5f,0x5e,0x5d,0x5b,0xc3});break;
    case SiteId::slot_materialize:bytes(s.continuation,{0x5d,0x5b,0x8d,0x64,0x24,12,0xc3});break;
    case SiteId::wrapper_release:bytes(s.continuation,{0xb8,1,0,0,0,0x5d,0x5b,0xc3});break;
    case SiteId::wrapper_cleanup:bytes(s.continuation,{0x5f,0xc3});bytes(0x4dcc7a,{0x5f,0xc3});break;
    case SiteId::wrapper_surface_replace:bytes(s.continuation,{0x8b,0xe5,0x5d,0xb8,1,0,0,0,0xc3});break;
    case SiteId::whole_reset:bytes(0x4c6190,{0xc3});bytes(s.continuation,{0x5e,0xb8,0,0,0,0,0xc3});break;
    case SiteId::forced_clear:bytes(0x4b9f70,{0xc3});bytes(s.continuation,{0x5f,0x5e,0x59,0xc3});break;
    case SiteId::recovery_clear:bytes(s.continuation,{0x59,0xc3});break;
    case SiteId::slot_name_replace:bytes(s.continuation,{0xc3});bytes(0x4f4c44,{0xc3});break;
    case SiteId::slot_publish_loaded:bytes(0x4ee360,{0xc3});bytes(s.continuation,{0xc3});break;
    case SiteId::restore_matched_search:bytes(s.continuation,{0xc3});bytes(0x498bdf,{0x83,0x48,0x2c,4,0x89,0x50,0x30,0xc3});break;
    default:bytes(s.continuation,{0xc3});break;
    }
}
}
// Reserve synthetic engine addresses with the PE loader. Real fixture text is
// linked above0x630000; no existing foreign allocation is replaced or written.
__attribute__((section(".x3map"),used)) unsigned char destination_fixture_map[0x21f000]{};
static bool owned_map(){
    const auto module=GetModuleHandleW(nullptr);
    if(ptr(module)!=0x400000||ptr(destination_fixture_map)!=0x401000||ptr(reinterpret_cast<void*>(&capture_exit))<0x630000)return false;
    for(const unsigned base:{0x490000u,0x600000u}){
        const unsigned size=base==0x490000?0x90000:0x20000;
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(reinterpret_cast<void*>(base),&info,sizeof info)!=sizeof info||
           info.AllocationBase!=module||info.Type!=MEM_IMAGE||info.State!=MEM_COMMIT||
           base<ptr(destination_fixture_map)||base+size>ptr(destination_fixture_map)+sizeof destination_fixture_map||
           base+size>ptr(info.BaseAddress)+info.RegionSize)return false;
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(base),size,base==0x490000?PAGE_EXECUTE_READWRITE:PAGE_READWRITE,&old))return false;
    }
    return true;
}
unsigned destination_abi_checks(){
    if(!owned_map()){check(false,"owned PE synthetic address space unavailable");return failures;}
    void* engine=reinterpret_cast<void*>(0x490000);
    void* data=reinterpret_cast<void*>(0x600000);
    auto* stack=static_cast<unsigned char*>(VirtualAlloc(nullptr,1<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192*site_count,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    check(engine&&data&&stack&&code,"synthetic x86 address space allocations");if(!engine||!data||!stack||!code)return failures;
    for(unsigned pass=0;pass<2;++pass){
    auto* emitted=code+pass*4096*site_count;
    const auto helper=pass?dispatcher_address():ptr(reinterpret_cast<void*>(&abi_dispatch));
    Routes routes;for(unsigned i=0;i<site_count;++i){unsigned size=0;check(encode_stub(emitted+i*4096,2048,ptr(emitted+i*4096),i,helper,routes,size),"actual encoder");}
    check(FlushInstructionCache(GetCurrentProcess(),emitted,4096*site_count),"emitted stub cache flush");
    const auto stack_base=(ptr(stack)+(1<<19))&~15u;
    for(unsigned i=0;i<site_count;++i)for(unsigned alignment=0;alignment<4;++alignment)for(unsigned branch=0;branch<2;++branch){case_id=i*100+alignment*2+branch;
        tails(i);check(FlushInstructionCache(GetCurrentProcess(),engine,0x90000),"original synthetic body cache flush");
        initialize(stack_base+alignment*4,i,branch);entry_target=sites[i].address;enter_case();const auto expected=capture();
        initialize(stack_base+alignment*4,i,branch);g::Admission gate;gate.qualify_owner(GetCurrentThreadId());Source source;Destination destination(gate,source,GetCurrentThreadId());destination.set_device_key(0x100,GetCurrentThreadId());
        {std::lock_guard<std::mutex> lock(destination.domain());destination.state_locked().table(0x610000,4,0,false);}
        NativeMemory memory;Observer observer(destination,memory,routes,{});actual=&observer;if(pass)bind_dispatcher(&observer);
        entry_target=ptr(emitted+i*4096);SetLastError(0x12345678);enter_case();const auto got=capture();
        check(!std::memcmp(expected.regs,got.regs,sizeof got.regs),"GPR and exact incoming stack output");check((expected.flags&0xcd5)==(got.flags&0xcd5),"original arithmetic and direction flags");
        check(!std::memcmp(expected.xmm,got.xmm,128),"all XMM registers");check(!std::memcmp(expected.x87,got.x87,108),"complete x87 payload and environment");check(expected.mxcsr==got.mxcsr&&got.mxcsr==input_mxcsr,"MXCSR");check(got.error==0x12345678,"LastError");
        if(!pass)check(calls==(i==unsigned(SiteId::restore_matched_search)&&!branch?0u:2u),"paired actual Observer invocation");
    }
    }
    bind_dispatcher(nullptr);
    std::printf("MEDIA DESTINATION ABI checks=%u failures=%u sites=19 executions=304\n",checks,failures);return failures;
}
