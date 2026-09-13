// Exercise the actual production marker emitters and CPU callback boundary.
// Native code below is synthetic fixture code; the independent host site
// verifier binds the real game spans. This executable never launches the game.
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/game_phases.h"
#include "../../src/proxy/game_phase_sites.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
namespace phases=x3m::game_phases;
namespace patch=x3m::engine_patch;
namespace marker=x3m::game_phases::sites;
namespace x3m { void log(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::putchar('\n');} }
namespace x3m::telemetry { bool enabled(){return true;} std::uint64_t frequency(){LARGE_INTEGER f{};return QueryPerformanceFrequency(&f)?std::uint64_t(f.QuadPart):0;} }
namespace x3m::object_trace { bool executable_verified(){return true;} }
extern "C" {
struct Snapshot { std::uint32_t regs[9];unsigned char xmm[128],x87[108];std::uint32_t mxcsr; };
Snapshot* fixture_output=nullptr;
std::uint32_t fixture_entry_esp=0,fixture_exit_esp=0;
alignas(16) unsigned char fixture_input_x87[108];
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw=0x077f;
std::uint32_t fixture_mxcsr=0x3f80;
std::uint32_t fixture_flags=0x647,fixture_normal_cpu=0;
void fixture_call(std::uint32_t site,std::uint32_t args,std::uint32_t locals,std::uint32_t eax,std::uint32_t ecx,std::uint32_t edx);
}
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
"movl %esp,_fixture_entry_esp\n"
"pushfl\n pushal\n subl $256,%esp\n"
"fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
"movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
"movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
"movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
"fninit\n cmpl $0,_fixture_normal_cpu\n jne 1f\n fld1\n fldpi\n1:\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
"movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
"movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
"movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
"movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl _fixture_flags\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
"pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
"movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
"movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
"fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
"frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
"movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
"movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
"addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n");

static unsigned checks=0,failures=0;
static void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
static std::uint32_t callbacks[marker::Count]{},observed[marker::Count][9]{};
static std::uint32_t stack_args[marker::Count][5]{};
static bool capture_args=false;
static void __cdecl hostile_callback(unsigned kind,const std::uint32_t* regs){
    check(kind<marker::Count,"callback marker ID");if(kind>=marker::Count)return;
    ++callbacks[kind];std::memcpy(observed[kind],regs,sizeof observed[kind]);
    if(capture_args&&(kind==marker::DelayedBegin||kind==marker::ColdBegin||kind==marker::PresentBegin)){
        const auto* native=reinterpret_cast<const std::uint32_t*>(std::uintptr_t(regs[3])+4);
        const unsigned words=kind==marker::DelayedBegin?1:kind==marker::ColdBegin?2:5;
        std::memcpy(stack_args[kind],native,words*sizeof *native);
    }
    unsigned cw=0,mxcsr=0,flags=0;
    asm volatile("fnstcw %0\n stmxcsr %1\n pushfl\n popl %2":"=m"(cw),"=m"(mxcsr),"=r"(flags)::"memory");
    check((cw&0xffff)==0x037f,"callback receives default x87 control");
    check(mxcsr==0x1f80,"callback receives default MXCSR");
    check(!(flags&0x400),"callback C ABI has clear direction flag");
    // Volatile and computational state must be restored even when injected
    // work changes every XMM register, the x87 stack/control and LastError.
    SetLastError(0xcafebabe);
    const unsigned hostile_mxcsr=0x5f80;
    const unsigned short hostile_cw=0x0b7f;
    asm volatile("fninit\n fldln2\n fld1\n fldcw %0\n ldmxcsr %1\n"
                 "pxor %%xmm0,%%xmm0\n pxor %%xmm1,%%xmm1\n pxor %%xmm2,%%xmm2\n pxor %%xmm3,%%xmm3\n"
                 "pxor %%xmm4,%%xmm4\n pxor %%xmm5,%%xmm5\n pxor %%xmm6,%%xmm6\n pxor %%xmm7,%%xmm7"
                 ::"m"(hostile_cw),"m"(hostile_mxcsr):"memory","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7");
}
static void invoke(void* target,Snapshot& out){
    fixture_output=&out;SetLastError(0x13572468);
    fixture_call(std::uint32_t(std::uintptr_t(target)),0x23456789,0x3456789a,0x456789ab,0x56789abc,0x6789abcd);
    check(GetLastError()==0x13572468,"LastError preserved");
    check(fixture_entry_esp==fixture_exit_esp,"four-byte incoming caller stack balanced");
    check(!std::memcmp(fixture_input_x87,out.x87,108),"incoming x87 image survives actual callback");
}
static void compare(const Snapshot& before,const Snapshot& after){
    for(unsigned r=0;r<9;++r)if(r!=3)check(before.regs[r]==after.regs[r],"GPR and EFLAGS match native baseline");
    check(!std::memcmp(before.xmm,after.xmm,128),"all XMM registers match native baseline");
    check(before.mxcsr==after.mxcsr,"MXCSR matches native baseline");
    check(!std::memcmp(before.x87,after.x87,108),"x87 image matches native baseline");
}
static void* marker_stub(unsigned kind,void* next){
    void** slot=nullptr;void* stub=phases::fixture_emit(kind,&slot);
    check(stub&&slot,"actual production stub emitted");
    if(!stub||!slot)return nullptr;
    check(patch::store_pointer(slot,next),"continuation slot installed");
    return stub;
}
extern "C" {
std::uint32_t replay_calls=0,replay_request=0,replay_cockpit=0,replay_mode=0;
std::uint32_t replay_choice=0,replay_result=0x24681357,replay_context=0x76543210;
void replay_delayed();void replay_audio();void replay_cold();void replay_present();
}
// These are deliberately synthetic ABI witnesses, not extracted game bodies.
asm(".text\n.globl _replay_delayed\n_replay_delayed:\n"
"incl _replay_calls\n movl %eax,_replay_mode\n movl %ecx,_replay_cockpit\n"
"movl 4(%esp),%eax\n movl %eax,_replay_request\n movl $0x24681357,%eax\n ret $4\n"
".globl _replay_audio\n_replay_audio:\n incl _replay_calls\n movl 12(%esp),%eax\n movl %eax,_replay_request\n movl $1,%eax\n ret\n"
".globl _replay_cold\n_replay_cold:\n incl _replay_calls\n movl 4(%esp),%eax\n movl %eax,_replay_request\n movl _replay_result,%eax\n ret\n"
".globl _replay_present\n_replay_present:\n incl _replay_calls\n movl 4(%esp),%eax\n movl %eax,_replay_request\n movl $0x88760868,%eax\n ret $20\n");
static std::uintptr_t address(const void* p){return reinterpret_cast<std::uintptr_t>(p);}
static void push(patch::Emitter& e,std::uint32_t value){e.byte(0x68);e.dword(value);}
static void call(patch::Emitter& e,const void* function){e.byte(0xe8);e.rel32(function);}
struct Replay {void* body=nullptr;void* begin=nullptr;void* end=nullptr;unsigned first=0,last=0;};
static Replay make_replay(unsigned kind){
    patch::Emitter e(256);Replay r;r.body=e.here();
    if(kind==0){
        push(e,0x11112222);e.byte(0xb8);e.dword(3);e.byte(0xb9);e.dword(0x33334444);
        r.first=marker::DelayedBegin;r.last=marker::DelayedEnd;r.begin=e.here();call(e,reinterpret_cast<void*>(&replay_delayed));
        r.end=e.here();e.byte(0xe9);e.dword(5);e.byte(0xb8);e.dword(0xbad00001);e.byte(0xc3);
    }else if(kind==1){
        e.byte(0xbe);e.dword(0x33334444);e.byte(0xbb);e.dword(0x11112222);e.byte(0x33);e.byte(0xed);
        r.first=marker::AcquisitionBegin;r.last=marker::AcquisitionEnd;r.begin=e.here();
        const unsigned char arguments[]={0x6a,0xff,0x6a,0x01,0x55,0x55};e.bytes(arguments,sizeof arguments);
        call(e,reinterpret_cast<void*>(&replay_audio));e.byte(0x83);e.byte(0xc4);e.byte(0x10);
        e.byte(0x83);e.byte(0x3d);e.dword(std::uint32_t(address(&replay_choice)));e.byte(0);
        e.byte(0x74);e.byte(14); // skip the complete conditional cdecl call
        e.bytes(arguments,sizeof arguments);call(e,reinterpret_cast<void*>(&replay_audio));
        e.byte(0x83);e.byte(0xc4);e.byte(0x10);
        r.end=e.here();e.byte(0x8b);e.byte(0x15);e.dword(std::uint32_t(address(&replay_context)));e.byte(0xc3);
    }else if(kind==2){
        push(e,0x20);push(e,0x11112222);r.first=marker::ColdBegin;r.last=marker::ColdEnd;
        r.begin=e.here();call(e,reinterpret_cast<void*>(&replay_cold));r.end=e.here();
        const unsigned char ending[]={0x83,0xc4,0x08,0x85,0xc0,0x75,0x05,0xb8,0x02,0,0xad,0x0b,0xc3};
        e.bytes(ending,sizeof ending);
    }else{
        static std::uint32_t vtable[18]{};vtable[17]=std::uint32_t(address(reinterpret_cast<void*>(&replay_present)));
        e.byte(0xba);e.dword(std::uint32_t(address(vtable)));
        for(unsigned i=0;i<4;++i){push(e,0);}
        push(e,0x11112222);
        r.first=marker::PresentBegin;r.last=marker::PresentEnd;r.begin=e.here();
        const unsigned char dispatch[]={0x8b,0x42,0x44,0xff,0xd0};e.bytes(dispatch,sizeof dispatch);r.end=e.here();
        const unsigned char ending[]={0x3d,0x68,0x08,0x76,0x88,0x75,0x05,0xb8,0x03,0,0xad,0x0b,0xc3};
        e.bytes(ending,sizeof ending);
    }
    if(!e.finish())r.body=nullptr;
    return r;
}
static bool install_replay_marker(patch::Site& owned,unsigned kind,void* at){
    auto spec=marker::kSites[kind];spec.address=address(at);
    // Native branch destinations/global pointers belong to this fixture.
    // Length, operand form and relocation metadata remain the actual contract.
    std::memcpy(spec.expected,at,spec.length);
    if(!patch::claim(owned,spec)){std::printf("FAIL claim %u %s\n",kind,owned.status);check(false,"fixture span claimed");return false;}
    void* stub=marker_stub(kind,owned.tail);if(!stub)return false;
    patch::push_front(owned,stub);return true;
}
static void replay_checks(unsigned kind,unsigned variant){
    Replay r=make_replay(kind);check(r.body!=nullptr,"synthetic native body emitted");if(!r.body)return;
    replay_choice=variant;replay_result=variant?0:0x24681357;
    Snapshot baseline{},hooked{};replay_calls=0;invoke(r.body,baseline);
    const unsigned native_calls=replay_calls;
    check(native_calls==(kind==1?1+variant:1),"baseline executes exact native call count");
    patch::Site before{},after{};
    if(!install_replay_marker(before,r.first,r.begin)||!install_replay_marker(after,r.last,r.end))return;
    const unsigned begin_calls=callbacks[r.first],end_calls=callbacks[r.last];
    capture_args=true;replay_calls=0;invoke(r.body,hooked);capture_args=false;compare(baseline,hooked);
    check(replay_calls==native_calls,"instrumented native calls execute exactly once each");
    check(callbacks[r.first]==begin_calls+1&&callbacks[r.last]==end_calls+1,"paired endpoints both observed once");
    const auto begin_esp=observed[r.first][3]+4,end_esp=observed[r.last][3]+4;
    check(end_esp==begin_esp+(kind==0?4:kind==3?20:0),"native cdecl/custom/stdcall cleanup preserved");
    if(kind==0){
        check(replay_mode==3&&replay_cockpit==0x33334444&&replay_request==0x11112222,"native custom-register inputs preserved");
        check(observed[r.first][7]==3&&observed[r.first][6]==0x33334444&&stack_args[r.first][0]==0x11112222,"delayed marker sees original ABI inputs");
        check(hooked.regs[7]==0x24681357,"relocated JMP bypasses dead fallthrough");
    }else if(kind==1){
        check(observed[r.first][1]==0x33334444&&observed[r.first][4]==0x11112222,"acquisition captures requested target and cockpit");
        check(replay_request==1,"native sound ID argument preserved");
        check(hooked.regs[5]==replay_context,"acquisition endpoint global load replayed");
    }else if(kind==2){
        check(stack_args[r.first][0]==0x11112222&&stack_args[r.first][1]==0x20,"cold-load stack inputs preserved");
        check(hooked.regs[7]==(variant?0x0bad0002:0x24681357),"replayed TEST drives the native conditional branch");
    }else{
        check(stack_args[r.first][0]==0x11112222,"Present begin captures original raw device");
        for(unsigned i=1;i<5;++i)check(stack_args[r.first][i]==0,"Present four null arguments preserved");
        check(observed[r.last][7]==0x88760868,"Present end observes native HRESULT before comparison");
        check(hooked.regs[7]==0x0bad0003,"Present HRESULT CMP replay controls native branch");
    }
    check(patch::restore(after)&&patch::restore(before),"synthetic marker rollback restores both spans");
}
static inline double number(std::uint64_t value){
    return double(std::uint32_t(value>>32))*4294967296.0+double(std::uint32_t(value));
}
static constexpr std::uint32_t benchmark_raw_device=0x11112222;
static void* benchmark_begin_adapter(void* next){
    patch::Emitter e(64);void* start=e.here();
    for(unsigned i=0;i<4;++i){push(e,0);}
    push(e,benchmark_raw_device);e.byte(0xe9);e.rel32(next);
    return e.finish()?start:nullptr;
}
static bool timed_loops(void* continuation,void* const* stubs,void* const* present_begin,
                        unsigned mode,unsigned loops,std::uint64_t& ticks){
    phases::fixture_enable(mode==2);Snapshot output{};fixture_output=&output;
    const auto one_loop=[&](unsigned frame){
        // The motion route advances this epoch once per frame. Include the
        // common advance in all three modes; enabled metadata reads then pay
        // their ordinary first-touch region validation in every loop.
        x3m::engine_memory::next_frame();
        for(unsigned marker_id=0;marker_id<13;++marker_id){
            void* target=mode?stubs[marker_id]:continuation;
            fixture_call(std::uint32_t(address(target)),0,0,0,0,0);
        }
        // The native caller has pushed device plus four null arguments before
        // PresentBegin. Both adapters reproduce those pushes and cleanup; only
        // the hooked adapter dispatches the actual marker callback between them.
        fixture_call(std::uint32_t(address(present_begin[mode?1:0])),0,0,0,0,0);
        LARGE_INTEGER endpoint{};
        if(!QueryPerformanceCounter(&endpoint)||endpoint.QuadPart<=0)return false;
        phases::present_endpoint(benchmark_raw_device,1,1,frame,false,
                                 std::uint64_t(endpoint.QuadPart),0);
        fixture_call(std::uint32_t(address(mode?stubs[marker::PresentEnd]:continuation)),0,0,0,0,0);
        fixture_call(std::uint32_t(address(mode?stubs[marker::Tail]:continuation)),0,0,0,0,0);
        return true;
    };
    // Prime one completed dispatch outside the paired timing window. Every
    // measured loop now has a real anchor, appends finite normal tape segments,
    // stages its owned endpoint, and closes the dispatch with matching HRESULT.
    // Runtime report totals include this unmeasured warm-up loop per batch.
    if(!one_loop(0))return false;
    LARGE_INTEGER start{},end{};
    if(!QueryPerformanceCounter(&start)||start.QuadPart<=0)return false;
    for(unsigned loop=0;loop<loops;++loop)if(!one_loop(loop+1))return false;
    if(!QueryPerformanceCounter(&end)||end.QuadPart<start.QuadPart)return false;
    ticks=std::uint64_t(end.QuadPart-start.QuadPart);return true;
}
static void benchmark(void* continuation,void* const* stubs){
    constexpr unsigned loops=4,batches=128,trials=3;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"benchmark QPC frequency");
    if(frequency.QuadPart<=0)return;
    phases::fixture_set_callback(nullptr);fixture_normal_cpu=1;
    fixture_cw=0x037f;fixture_mxcsr=0x1f80;fixture_flags=0x247;
    // Native stdcall has consumed all five words before the next observation.
    patch::Emitter cleanup(8);void* clean=cleanup.here();
    const unsigned char pop_arguments[]={0x83,0xc4,0x14,0xc3};
    cleanup.bytes(pop_arguments,sizeof pop_arguments);
    check(cleanup.finish()!=nullptr,"benchmark Present argument cleanup emitted");
    void* begin_stub=marker_stub(marker::PresentBegin,clean);
    void* present_begin[2]={benchmark_begin_adapter(clean),benchmark_begin_adapter(begin_stub)};
    check(begin_stub&&present_begin[0]&&present_begin[1],"paired Present benchmark adapters emitted");
    if(!begin_stub||!present_begin[0]||!present_begin[1])return;
    // Ask Windows for an unused region. A fixture-only address seam keeps the
    // actual checked reads/cache path without depending on free game addresses.
    void* const pump_globals=VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(pump_globals!=nullptr,"dynamic synthetic pump-global mapping allocated");
    if(!pump_globals)return;
    auto* const bytes=static_cast<unsigned char*>(pump_globals);
    *reinterpret_cast<std::uint32_t*>(bytes+0x8adc)=1; // active window
    *reinterpret_cast<std::uint32_t*>(bytes+0x6f3c)=std::uint32_t(address(bytes+0x9000));
    *reinterpret_cast<std::uint32_t*>(bytes+0x9000)=0; // ordinary active-mode flags
    const bool configured=phases::fixture_pump_region(address(pump_globals),0x10000);
    check(configured,"bounded fixture pump address region configured");
    if(!configured){VirtualFree(pump_globals,0,MEM_RELEASE);return;}
    std::uint64_t totals[3]{};bool valid=true;
    for(unsigned trial=0;trial<trials&&valid;++trial)
        for(unsigned batch=0;batch<batches&&valid;++batch)
            for(unsigned position=0;position<3&&valid;++position){
                // Balanced order; initialization and mode switching are excluded.
                const unsigned mode=(position+trial+1)%3;std::uint64_t ticks=0;
                valid=timed_loops(continuation,stubs,present_begin,mode,loops,ticks);totals[mode]+=ticks;
            }
    check(valid,"paired baseline disabled enabled QPC samples");
    const double scale=1e6/number(std::uint64_t(frequency.QuadPart))/double(loops*batches*trials);
    std::printf("GAME PHASE BENCH trials=%u batches=%u loops_per_batch=%u marker_calls_per_loop=16 owned_bridges_per_loop=1 cpu_queries_per_enabled_loop=15 baseline_loop_us=%.6f disabled_loop_us=%.6f enabled_loop_us=%.6f disabled_added_loop_us=%.6f enabled_added_loop_us=%.6f enabled_added_per_marker_equivalent_us=%.6f scope=actual_emit_callback_core_owned_bridge_GetThreadTimes harness=paired_same_snapshot normal_tape=yes pump_metadata=valid engine_read_epoch=per_loop warmup_loops_per_batch=1 runtime_report_includes_warmup=yes game_fps=unmeasured\n",
        trials,batches,loops,number(totals[0])*scale,number(totals[1])*scale,number(totals[2])*scale,
        (number(totals[1])-number(totals[0]))*scale,(number(totals[2])-number(totals[0]))*scale,
        (number(totals[2])-number(totals[0]))*scale/16.0);
    // This is outside the measured batches. The production report supplies raw
    // GetThreadTimes query-sandwich totals/maxima and actual handler costs.
    phases::report(0);
    phases::fixture_enable(false);
    check(phases::fixture_pump_region(0,0),"fixture pump address region cleared before free");
    x3m::engine_memory::reset();
    check(VirtualFree(pump_globals,0,MEM_RELEASE)!=FALSE,"synthetic pump-global mapping released");
}
int main(){
    for(unsigned i=0;i<sizeof fixture_xmm_seed;++i)fixture_xmm_seed[i]=static_cast<unsigned char>(i*37+9);
    patch::Emitter tail(8);void* continuation=tail.here();tail.byte(0xc3);if(!tail.finish())return 2;
    phases::fixture_enable(false);phases::fixture_set_callback(&hostile_callback);
    void* stubs[marker::Count]{};
    for(unsigned kind=0;kind<marker::Count;++kind){
        stubs[kind]=marker_stub(kind,continuation);if(!stubs[kind])return 2;
        Snapshot baseline{},hooked{};invoke(continuation,baseline);invoke(stubs[kind],hooked);compare(baseline,hooked);
        check(callbacks[kind]==1,"every actual emitter reaches redirected callback once");
        check(observed[kind][0]==0x98765432&&observed[kind][1]==0x12345678&&
              observed[kind][2]==0x3456789a&&observed[kind][4]==0x23456789&&
              observed[kind][5]==0x6789abcd&&observed[kind][6]==0x56789abc&&observed[kind][7]==0x456789ab,
              "callback PUSHAD frame preserves all incoming register values");
    }
    replay_checks(0,0);replay_checks(1,0);replay_checks(1,1);
    replay_checks(2,0);replay_checks(2,1);replay_checks(3,0);
    benchmark(continuation,stubs);
    std::printf("GAME PHASE CPU stubs=23 replay_cases=6 checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
