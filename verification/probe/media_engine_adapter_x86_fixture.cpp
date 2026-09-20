// Native x86 emitted-code ABI witness. Build/run is owned by the root Wine
// queue. This executable never starts the game or reads its files.
#include "../../src/proxy/media_engine_adapter.h"
#include "../../src/proxy/cpu_state.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <excpt.h>
using namespace x3m::media_engine;
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
static Routes routing;static unsigned mode,helper_calls,checks,failures,case_id;
static std::uint32_t chosen_target;
static Consumer* semantic_consumer=nullptr;
static bool semantic_mode=false;
static unsigned observed_after[return_count]{};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL case=%u line=%u %s\n",case_id,__LINE__,#x);}}while(0)
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl fixture_dispatch(Frame* frame,unsigned site) noexcept {
    x3m::PreserveCpuState cpu;
    ++helper_calls;
    // Deliberately dirty every injected helper's volatile computational domain.
    SetLastError(0xdeadbeef);
    asm volatile("fninit\n fld1\n fld1\n xorps %%xmm0,%%xmm0\n xorps %%xmm1,%%xmm1\n xorps %%xmm2,%%xmm2\n xorps %%xmm3,%%xmm3\n xorps %%xmm4,%%xmm4\n xorps %%xmm5,%%xmm5\n xorps %%xmm6,%%xmm6\n xorps %%xmm7,%%xmm7"
        :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
    unsigned mx=0x1f80;asm volatile("ldmxcsr %0"::"m"(mx):"memory");
    if(semantic_mode){
        semantic_consumer->dispatch(static_cast<SiteId>(site),*frame);
        if(site>=unsigned(SiteId::pump_return)&&site<unsigned(SiteId::pump_return)+return_count)
            observed_after[site-unsigned(SiteId::pump_return)]=frame->target;
        return;
    }
    if(site<site_count)frame->target=mode==0?routing.return_plain:mode==1?routing.forward[site]:chosen_target;
    else if(site>=unsigned(SiteId::pump_return)&&site<unsigned(SiteId::pump_return)+return_count){
        const auto index=site-unsigned(SiteId::pump_return);
        frame->target=mode==3?chosen_target:sites[unsigned(guarded_sites[index])].continuation;
    }
}
static void put(unsigned at,unsigned value){std::memcpy(reinterpret_cast<void*>(at),&value,4);}
static void jump(unsigned at,unsigned target){auto* p=reinterpret_cast<unsigned char*>(at);p[0]=0xe9;put(at+1,target-at-5);}
static unsigned ptr(const void* p){return reinterpret_cast<unsigned>(p);}
static void reset_world(){
    std::memset(reinterpret_cast<void*>(0x490000),0xcc,0x90000);
    std::memset(reinterpret_cast<void*>(0x600000),0,0x20000);
    for(const auto& site:sites){
        if(site.original_call)*reinterpret_cast<unsigned char*>(site.original_call)=0xc3;
    }
    // Replay CALL targets not represented by call-replacement rows.
    *reinterpret_cast<unsigned char*>(0x4d0600)=0xc3;
    *reinterpret_cast<unsigned char*>(0x4d0430)=0xc3;
    *reinterpret_cast<unsigned char*>(0x50e1b0)=0xc3;
    *reinterpret_cast<unsigned char*>(0x510000)=0xc3; // cdecl callback
    const unsigned char stdcall[]={0xc2,4,0};std::memcpy(reinterpret_cast<void*>(0x510010),stdcall,3);
    put(0x606f44,0x610400);put(0x6085ec,0);
    put(0x610000+0x24,0x610100);put(0x610000+0x18,7);
    put(0x610100,0x610200);put(0x610100+4,0);put(0x610100+0x8c,8);
    put(0x610200+0x20,0x510010);put(0x610200+0x48,0x510010);
}
static void initialize_input(unsigned stack){
    for(unsigned i=0;i<8;++i)input_gpr[i]=0x11110000+i*0x111;
    input_gpr[0]=0x610000;input_gpr[1]=0x610000;input_gpr[2]=0;input_gpr[3]=stack;
    input_gpr[6]=0x610000;input_gpr[7]=0x610000;
    for(unsigned i=0;i<sizeof input_xmm;++i)input_xmm[i]=static_cast<unsigned char>(i*7+3);
    std::memset(reinterpret_cast<void*>(stack-1024),0x55,2048);
    put(stack,ptr(reinterpret_cast<void*>(&capture_exit)));put(stack+4,123);put(stack+8,8);
    entry_stack=stack;helper_calls=0;SetLastError(0x12345678);
}
static void check_computational(){
    CHECK(!std::memcmp(input_xmm,output_xmm,sizeof input_xmm));
    CHECK(!std::memcmp(input_x87,output_x87,sizeof input_x87));
    CHECK(output_mxcsr==input_mxcsr);CHECK(GetLastError()==0x12345678);
}
static void execute(){
    // Cache flush is checked, even for the fixture's fake engine continuations.
    CHECK(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x490000),0x90000)!=FALSE);
    SetLastError(0x12345678);enter_case();check_computational();
}
struct IdleServices:Services {
    bool ready()const noexcept override{return false;}
    bool reserve(x3m::media::SessionHandle,x3m::media::SourceKey)noexcept override{return false;}
    bool observe_record(x3m::media::SessionHandle,const BindingObservation&)noexcept override{return false;}
    void cancel(x3m::media::SessionHandle)noexcept override{}
    void publish(x3m::media_playback::Adapter&,x3m::media::SessionHandle)noexcept override{}
    PumpResult pump(const PumpRequest&,CurrentCheck,void*)noexcept override{return PumpResult::failed;}
    bool position(x3m::media::SessionHandle,std::uint32_t&)noexcept override{return false;}
    x3m::media_playback::ClockTransaction clock(x3m::media::SessionHandle)noexcept override{return {};}
};
extern "C" {
unsigned exception_stub,outer_stub,exception_count,backend_count;
bool raise_in_backend=true;
void exception_resume();
// Fixture-owned continuable SEH exception: redirect to the existing catch frame,
// restoring FS registration there. Production installs no exception handler.
EXCEPTION_DISPOSITION __cdecl fixture_exception(EXCEPTION_RECORD* record,void* registration,CONTEXT* context,void*) {
    if(record->ExceptionCode!=0xe0424242||record->ExceptionFlags)return ExceptionContinueSearch;
    ++exception_count;context->Esp=ptr(registration);context->Eip=ptr(reinterpret_cast<void*>(&exception_resume));
    return ExceptionContinueExecution;
}
__attribute__((naked)) void exception_resume(){asm volatile(
 "mov (%esp),%eax\n mov %eax,%fs:0\n add $8,%esp\n popal\n popfl\n ret\n");}
__attribute__((naked)) void catch_inner(){asm volatile(
 "pushfl\n pushal\n push $_fixture_exception\n pushl %fs:0\n mov %esp,%fs:0\n"
 "mov $0x610100,%eax\n call *_exception_stub\n jmp _exception_resume\n");}
HRESULT __stdcall throwing_backend(void*) {
    ++backend_count;
    if(raise_in_backend)RaiseException(0xe0424242,0,0,nullptr);
    return S_OK;
}
void __cdecl outer_callback(unsigned,unsigned){catch_inner();}
__attribute__((naked)) void run_outer(){asm volatile(
 "pushfl\n pushal\n mov $_outer_callback,%eax\n xor %ecx,%ecx\n call *_outer_stub\n popal\n popfl\n ret\n");}
}
extern "C" {
unsigned allocation_calls,free_calls,last_free;bool allocation_allowed=true;
void* __cdecl matched_allocate(unsigned bytes){++allocation_calls;return allocation_allowed&&bytes==0xb4?reinterpret_cast<void*>(0x610500):nullptr;}
void __cdecl matched_free(void* p){++free_calls;last_free=ptr(p);}
}
static void allocation_contract(unsigned char* code,unsigned stack_base){
    case_id=9000;reset_world();
    jump(0x5112c4,ptr(reinterpret_cast<void*>(&matched_allocate)));
    jump(0x50e1b0,ptr(reinterpret_cast<void*>(&matched_free)));
    CHECK(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x490000),0x90000)!=FALSE);
    NativeMemory memory;const auto allocated=memory.allocate_shell();CHECK(allocated==0x610500&&allocation_calls==1);
    CHECK(*reinterpret_cast<unsigned*>(0x6085f4)==0xb4&&*reinterpret_cast<unsigned*>(0x6085f8)==0xb4);
    CHECK(*reinterpret_cast<unsigned*>(0x6089f8)==0xb4&&*reinterpret_cast<unsigned*>(0x6089fc)==1);
    memory.release_unpublished_shell(allocated);CHECK(free_calls==1&&last_free==allocated);
    CHECK(*reinterpret_cast<unsigned*>(0x6085f4)==0&&*reinterpret_cast<unsigned*>(0x6085f8)==0);
    CHECK(*reinterpret_cast<unsigned*>(0x6089f8)==0xb4&&*reinterpret_cast<unsigned*>(0x6089fc)==1);
    allocation_allowed=false;CHECK(memory.allocate_shell()==0&&allocation_calls==2);
    CHECK(*reinterpret_cast<unsigned*>(0x6089fc)==1&&free_calls==1);
    // Actual matching destructor free tail, with no COM cleanup in the fixture.
    const unsigned char tail[]={0x56,0xe8,0xd7,0xc3,3,0,0xb8,0xb4,0,0,0,
        0x29,5,0xf4,0x85,0x60,0,0x83,0xc4,4,0x29,5,0xf8,0x85,0x60,0,0x5f,0x5e,0xc3};
    std::memcpy(reinterpret_cast<void*>(0x4d1dd3),tail,sizeof tail);
    for(unsigned align=0;align<4;++align){case_id=9010+align;
        initialize_input(stack_base+align*4);input_gpr[7]=0x610500;
        put(0x6085f4,1000);put(0x6085f8,2000);
        mode=2;chosen_target=routing.destroy_free;entry_target=ptr(code);execute();
        CHECK(output_gpr[3]==entry_stack+4&&output_gpr[7]==0xb4);
        CHECK(output_gpr[0]==input_gpr[0]&&output_gpr[1]==input_gpr[1]);
        CHECK(*reinterpret_cast<unsigned*>(0x6085f4)==1000-0xb4&&*reinterpret_cast<unsigned*>(0x6085f8)==2000-0xb4);
        CHECK(last_free==0x610500&&free_calls==2+align);
    }
}
static void exception_scopes(unsigned char* code){
    case_id=8000;reset_world();
    x3m::media_playback::Adapter state;NativeMemory memory;IdleServices services;
    Consumer consumer(state,memory,services,routing);OwnerDomain domain{};CHECK(query_native_owner(domain));
    CHECK(consumer.bind_owner(domain,&native_execution_point));semantic_consumer=&consumer;semantic_mode=true;
    exception_stub=ptr(code+unsigned(SiteId::pause_call)*4096);
    outer_stub=ptr(code+unsigned(SiteId::callback_call)*4096);
    jump(0x510010,ptr(reinterpret_cast<void*>(&throwing_backend)));
    *reinterpret_cast<unsigned char*>(0x4982f2)=0xc3;
    *reinterpret_cast<unsigned char*>(0x498353)=0xc3;
    CHECK(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x490000),0x90000)!=FALSE);
    // More than32 escaping original exceptions cannot exhaust the bounded value
    // stack. The next normal return must reach the ordinary original tail.
    for(unsigned i=0;i<100;++i)catch_inner();
    CHECK(exception_count==100&&backend_count==100);
    raise_in_backend=false;catch_inner();CHECK(backend_count==101&&observed_after[2]==0x4982f2);
    // Inner exception is caught inside the original outer callback; its guard
    // still reaches the normal outer continuation after pruning the abandoned one.
    raise_in_backend=true;run_outer();CHECK(exception_count==101&&backend_count==102);
    CHECK(observed_after[4]==0x498353);
    semantic_mode=false;semantic_consumer=nullptr;
}
struct ReadyServices:IdleServices {
    bool ready()const noexcept override{return true;}
    bool reserve(x3m::media::SessionHandle,x3m::media::SourceKey)noexcept override{return true;}
    bool observe_record(x3m::media::SessionHandle,const BindingObservation&)noexcept override{return true;}
};
struct ExtendedFrame {Frame frame{};unsigned arguments[16]{};ExtendedFrame(){frame.saved_esp=ptr(&frame)+sizeof(Frame)-8;}};
extern "C" __attribute__((naked)) unsigned domain_call(unsigned,unsigned){asm volatile(
 "push %ebx\n push %esi\n push %edi\n push %ebp\n mov 24(%esp),%eax\n call *20(%esp)\n pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n");}
struct ForeignCall {Consumer* consumer;unsigned entry;bool okay=false;};
static DWORD WINAPI foreign_domain(void* opaque){
    auto& request=*static_cast<ForeignCall*>(opaque);
    const unsigned result=domain_call(request.entry,0x610000);
    x3m::media::SessionHandle session{7,77};x3m::media_playback::EngineKey key{0x1234,55};
    const bool lookup=request.consumer->binding_owner(0x610000,session,key);
    ExtendedFrame frame;frame.frame.eax=0x610900;
    request.consumer->dispatch(SiteId::run,frame.frame);
    request.okay=result==0&&!lookup&&session.slot==7&&session.generation==77&&key.address==0x1234&&key.generation==55&&
        frame.frame.target==routing.unobserved[unsigned(SiteId::run)];
    return 0;
}
static void native_domains(unsigned char* code,unsigned stack_base){
    case_id=10000;reset_world();allocation_allowed=true;
    jump(0x5112c4,ptr(reinterpret_cast<void*>(&matched_allocate)));
    jump(0x50e1b0,ptr(reinterpret_cast<void*>(&matched_free)));
    CHECK(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x490000),0x90000)!=FALSE);
    x3m::media_playback::Adapter state;NativeMemory memory;ReadyServices service;
    Consumer consumer(state,memory,service,routing);OwnerDomain domain{};
    CHECK(query_native_owner(domain)&&consumer.bind_owner(domain,&native_execution_point));
    CHECK(consumer.enable({true,true,true,true,true,true,true,true}));
    ExtendedFrame construction;construction.frame.esi=0x610000;construction.frame.ebx=2;construction.frame.edi=8;
    construction.arguments[1]=2;construction.arguments[2]=8;
    put(0x610000+0x2c,4);put(0x610000+0x30,0x3d);
    consumer.dispatch(SiteId::construct,construction.frame);
    CHECK(construction.frame.eax==0x610500&&construction.frame.target==routing.return_plain);
    x3m::media::SessionHandle session{};x3m::media_playback::EngineKey key{};
    CHECK(consumer.binding_owner(0x610000,session,key)&&key.address==0x610000);
    semantic_consumer=&consumer;semantic_mode=true;
    ForeignCall request{&consumer,ptr(code+unsigned(SiteId::run)*4096)};
    HANDLE thread=CreateThread(nullptr,0,&foreign_domain,&request,0,nullptr);CHECK(thread!=nullptr);
    if(thread){const auto waited=WaitForSingleObject(thread,5000);CHECK(waited==WAIT_OBJECT_0);
        if(waited!=WAIT_OBJECT_0)ExitProcess(3);
        CloseHandle(thread);CHECK(request.okay);}
    CHECK(consumer.admission_closed()&&!consumer.enable({true,true,true,true,true,true,true,true}));
    // Actual execution on another allocation, with the SAME Windows thread ID,
    // must refuse the owned record and preserve the whole injected envelope.
    initialize_input(stack_base);entry_target=ptr(code+unsigned(SiteId::run)*4096);input_gpr[7]=0x610000;
    execute();CHECK(output_gpr[7]==0&&output_gpr[3]==entry_stack+4);
    CHECK(consumer.binding_owner(0x610000,session,key)); // valid owner still has access
    // Owner-thread retirement remains available after admission is permanently closed.
    ExtendedFrame retirement;retirement.frame.esi=0x610000;consumer.dispatch(SiteId::retire_record,retirement.frame);
    CHECK(!consumer.binding_owner(0x610000,session,key));
    semantic_mode=false;semantic_consumer=nullptr;
}
// PE-loader reservation, following the established chase/startup fixture.
// Real fixture sections are linked at >=0x630000; no occupied mapping is replaced.
__attribute__((section(".x3map"),used)) unsigned char fixture_map[0x21f000]{};
static bool owned_map(){
    const auto module=GetModuleHandleW(nullptr);
    if(ptr(module)!=0x400000||ptr(fixture_map)!=0x401000||ptr(reinterpret_cast<void*>(&capture_exit))<0x630000)return false;
    for(const unsigned base:{0x490000u,0x600000u}){
        const unsigned size=base==0x490000?0x90000:0x20000;
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(reinterpret_cast<void*>(base),&info,sizeof info)!=sizeof info||
           info.AllocationBase!=module||info.Type!=MEM_IMAGE||info.State!=MEM_COMMIT||
           base<ptr(fixture_map)||base+size>ptr(fixture_map)+sizeof fixture_map||
           base+size>ptr(info.BaseAddress)+info.RegionSize)return false;
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(base),size,base==0x490000?PAGE_EXECUTE_READWRITE:PAGE_READWRITE,&old))return false;
    }
    return true;
}
int main(){
    static_assert(sizeof(void*)==4,"x86 only");
    if(!owned_map()){std::printf("owned PE fixture map unavailable error=%lu\n",GetLastError());return 2;}
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096*site_count,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* stack=static_cast<unsigned char*>(VirtualAlloc(nullptr,16384,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!code||!stack){std::printf("fixed fixture allocation failed error=%lu\n",GetLastError());return 2;}
    for(unsigned i=0;i<site_count;++i){unsigned bytes=0;
        CHECK(encode_stub(code+i*4096,2048,ptr(code+i*4096),i,ptr(reinterpret_cast<void*>(&fixture_dispatch)),routing,bytes));}
    DWORD previous=0;CHECK(VirtualProtect(code,4096*site_count,PAGE_EXECUTE_READ,&previous)!=FALSE);
    CHECK(FlushInstructionCache(GetCurrentProcess(),code,4096*site_count)!=FALSE);
    const unsigned stack_base=(ptr(stack+8192)&~63u);
    for(unsigned align=0;align<4;++align)for(unsigned i=0;i<site_count;++i){
        case_id=align*1000+i;reset_world();initialize_input(stack_base+align*4);
        mode=0;entry_target=ptr(code+i*4096);execute();
        CHECK(helper_calls==1);CHECK(output_gpr[3]==entry_stack+4);
        for(unsigned reg=0;reg<8;++reg)if(reg!=3)CHECK(output_gpr[reg]==input_gpr[reg]);
        CHECK(output_flags==input_flags);
        // Actual unowned displaced instructions and every forward continuation.
        ++case_id;reset_world();initialize_input(stack_base+align*4);mode=1;
        const auto id=static_cast<SiteId>(i);const auto& site=sites[i];
        if(id==SiteId::pause_call||id==SiteId::audio_call)input_gpr[7]=0x610100;
        if(id==SiteId::callback_call||id==SiteId::end_callback||id==SiteId::error_callback||
           id==SiteId::speech_callback||id==SiteId::explicit_callback)input_gpr[7]=0x510000;
        if(!site.original_call)jump(site.continuation,ptr(reinterpret_cast<void*>(&capture_exit)));
        unsigned expected[8];std::memcpy(expected,input_gpr,sizeof expected);
        expected[3]=entry_stack+(site.original_call?4:0);
        if(id==SiteId::explicit_play){expected[7]=input_gpr[1];expected[3]-=4;}
        if(id==SiteId::seek){expected[2]=entry_stack-4;expected[3]=(entry_stack-4)&~63u;}
        if(id==SiteId::run){expected[4]=0x610100;expected[3]-=0x74;}
        if(id==SiteId::position){expected[7]=0x610100;expected[3]-=12;}
        if(id==SiteId::stop){expected[1]=0x610100;expected[3]-=4;}
        if(id==SiteId::destroy_shell){expected[1]=input_gpr[7];expected[0]=0;expected[3]-=8;}
        if(id==SiteId::retire_record)expected[7]=7;
        if(id==SiteId::shutdown)expected[7]=0x610400;
        if(id==SiteId::rate)expected[7]=0x610100;
        if(id==SiteId::stop_all)expected[0]=0x610100;
        if(id==SiteId::speech_play){expected[6]=0x55555555;expected[5]=0x55555555;}
        if(id==SiteId::pause_call||id==SiteId::audio_call){expected[6]=0x610200;expected[5]=0x510010;}
        if(id==SiteId::position_call)expected[7]=input_gpr[0];
        if(id==SiteId::end_callback||id==SiteId::speech_callback)expected[3]+=4;
        execute();for(unsigned reg=0;reg<8;++reg)CHECK(output_gpr[reg]==expected[reg]);
        CHECK((output_flags&0x400)==(input_flags&0x400));
    }
    // Every caller-sensitive owned branch has exact P-to-B stack adjustment.
    const unsigned tails[]={routing.manager_drop4,routing.manager_drop8,routing.manager_next8,routing.speech_drop4,routing.manager_error8,
        0x498ce8,0x498d7a,0x498f08,0x498f7f,0x498697,0x498322,0x498362,0x498dd8,0x498fd2};
    const unsigned pops[]={4,8,8,4,8,0,0,0,0,0,0,0,0,0};
    for(unsigned align=0;align<4;++align)for(unsigned i=0;i<sizeof tails/sizeof tails[0];++i){
        case_id=5000+align*100+i;reset_world();initialize_input(stack_base+align*4);mode=2;chosen_target=tails[i];entry_target=ptr(code);
        for(const unsigned target:{0x4984beu,0x4984b5u,0x498fd2u,0x498473u})jump(target,ptr(reinterpret_cast<void*>(&capture_exit)));
        if(i>=5)jump(tails[i],ptr(reinterpret_cast<void*>(&capture_exit)));
        execute();CHECK(output_gpr[3]==entry_stack+pops[i]);
        for(unsigned reg=0;reg<8;++reg)if(reg!=3)CHECK(output_gpr[reg]==input_gpr[reg]);
        CHECK(output_flags==input_flags);
    }
    // Directly execute every after-envelope, including CALL replacements whose
    // return interception is exercised separately by the semantic consumer.
    for(unsigned align=0;align<4;++align)for(unsigned i=0;i<return_count;++i)for(unsigned stale=0;stale<2;++stale){
        case_id=6000+align*100+i*2+stale;reset_world();initialize_input(stack_base+align*4);
        mode=stale?3:1;entry_target=routing.after[i];
        chosen_target=i==10?0x498dd8u:i==9?0x498fd2u:i==1?routing.manager_drop4:
            (i==0||(i>=5&&i<=8))?0x4984beu:0x498362u;
        const unsigned live=sites[unsigned(guarded_sites[i])].continuation;
        jump(live,ptr(reinterpret_cast<void*>(&capture_exit)));
        const unsigned stale_end=i==1?0x4984beu:chosen_target;
        jump(stale_end,ptr(reinterpret_cast<void*>(&capture_exit)));
        execute();CHECK(helper_calls==1);CHECK(output_gpr[3]==entry_stack+(stale&&i==1?4:0));
        for(unsigned reg=0;reg<8;++reg)if(reg!=3)CHECK(output_gpr[reg]==input_gpr[reg]);
        CHECK(output_flags==input_flags);
    }
    // Execute the real epilogue register orders. Speech saves ESI before EDI;
    // explicit play saves EDI before ESI. A mislabeled failure tail swaps them.
    for(unsigned align=0;align<4;++align)for(unsigned speech=0;speech<2;++speech){
        case_id=7000+align*2+speech;reset_world();initialize_input(stack_base+align*4);
        const unsigned char explicit_end[]={0x5f,0x5e,0x5d,0x5b,0x59,0xc3};
        const unsigned char speech_end[]={0x5e,0x5f,0x5d,0x5b,0x59,0xc3};
        chosen_target=speech?0x498fd2:0x498d17;
        std::memcpy(reinterpret_cast<void*>(chosen_target),speech?speech_end:explicit_end,6);
        for(unsigned slot=0;slot<5;++slot)put(entry_stack+slot*4,0xabab0000+slot);
        put(entry_stack+20,ptr(reinterpret_cast<void*>(&capture_exit)));
        mode=2;entry_target=ptr(code);execute();CHECK(output_gpr[3]==entry_stack+24);
        CHECK(output_gpr[0]==0xabab0000u+speech);CHECK(output_gpr[1]==0xabab0001u-speech);
        CHECK(output_gpr[2]==0xabab0002&&output_gpr[4]==0xabab0003&&output_gpr[6]==0xabab0004);
    }
    exception_scopes(code);
    allocation_contract(code,stack_base);
    native_domains(code,stack_base);
    std::printf("sites=%u return_envelopes=%u alignments=4 checks=%u failures=%u\n",site_count,return_count,checks,failures);
    return failures?1:0;
}
