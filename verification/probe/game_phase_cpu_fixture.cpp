// Exercise the actual production marker emitters and CPU callback boundary.
// Native code below is synthetic fixture code; the independent host site
// verifier binds the real game spans. This executable never launches the game.
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/game_phases.h"
#include "../../src/proxy/game_phase_sites.h"
#include "../../src/proxy/frame_phases.h"
#include "../../src/proxy/frame_phase_sites.h"
#include "../../src/proxy/pass_phases.h"
#include "../../src/proxy/pass_phase_sites.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
namespace phases=x3m::game_phases;
namespace patch=x3m::engine_patch;
namespace marker=x3m::game_phases::sites;
namespace frame=x3m::frame_phases;
namespace frame_marker=x3m::frame_phases::sites;
constexpr unsigned total_stubs=marker::Count+frame_marker::Count; // frame stamps share the emitter, indexed after the phase group
namespace x3m::loading_trace {
void intervals_freeze(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,DWORD) noexcept {}
}
namespace x3m { LONGLONG dll_load_qpc=0; void log(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::putchar('\n');} }
namespace x3m::telemetry { bool enabled(){return true;} std::uint64_t frequency(){LARGE_INTEGER f{};return QueryPerformanceFrequency(&f)?std::uint64_t(f.QuadPart):0;} }
namespace x3m::sampling_profiler { void set_periodic(void (*)(std::uint64_t),unsigned) {} }
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
static std::uint32_t callbacks[total_stubs]{},observed[total_stubs][9]{};
static std::uint32_t stack_args[marker::Count][6]{};
static bool capture_args=false;
static void __cdecl hostile_callback(unsigned kind,const std::uint32_t* regs){
    check(kind<total_stubs,"callback marker ID");if(kind>=total_stubs)return;
    ++callbacks[kind];std::memcpy(observed[kind],regs,sizeof observed[kind]);
    if(capture_args&&(kind==marker::DelayedBegin||kind==marker::ColdBegin||kind==marker::PresentBegin||
                     kind==marker::PublisherBegin||kind==marker::PlaybackBegin||
                     kind==marker::CreateBegin||kind==marker::SeekBegin)){
        const auto* native=reinterpret_cast<const std::uint32_t*>(std::uintptr_t(regs[3])+4);
        const unsigned words=kind==marker::PlaybackBegin?6:kind==marker::PresentBegin?5:
            (kind==marker::ColdBegin||kind==marker::PublisherBegin)?2:1;
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
extern "C" {
std::uint32_t target_native_counts[4]{},target_play_args[6]{},target_create_args[2]{},target_seek_args[2]{};
std::uint32_t target_target=0x11112222,target_cockpit=0,target_mode=3;
void target_play_witness();void target_create_witness();void target_seek_witness();
}
// Playback's helper is called by its synthetic callee: its native arguments
// are two return PCs above ESP. The create/seek helpers are the direct callees.
asm(".text\n.globl _target_play_witness\n_target_play_witness:\n"
"incl _target_native_counts+4\n"
"movl 8(%esp),%eax\n movl %eax,_target_play_args\n movl 12(%esp),%eax\n movl %eax,_target_play_args+4\n"
"movl 16(%esp),%eax\n movl %eax,_target_play_args+8\n movl 20(%esp),%eax\n movl %eax,_target_play_args+12\n"
"movl 24(%esp),%eax\n movl %eax,_target_play_args+16\n movl 28(%esp),%eax\n movl %eax,_target_play_args+20\n ret\n"
".globl _target_create_witness\n_target_create_witness:\n incl _target_native_counts+8\n"
"movl %eax,_target_create_args+4\n movl 4(%esp),%eax\n movl %eax,_target_create_args\n movl $0x24681357,%eax\n ret\n"
".globl _target_seek_witness\n_target_seek_witness:\n incl _target_native_counts+12\n"
"movl %eax,_target_seek_args+4\n movl 4(%esp),%eax\n movl %eax,_target_seek_args\n movl _replay_result,%eax\n ret\n");
static constexpr std::uint32_t playback_args[6]={7,0,0,1,0xffffffff,0};
struct TargetReplay {
    void* body=nullptr;void* publisher_return=nullptr;
    void* spans[8]{};patch::Site owned[8]{};unsigned installed=0;
};
static TargetReplay make_target_replay(){
    TargetReplay r;
    patch::Emitter create(128);void* create_body=create.here();
    create.byte(0xbf);create.dword(2); // native shared join decrements EDI
    create.byte(0x83);create.byte(0x3d);create.dword(std::uint32_t(address(&replay_choice)));create.byte(1);
    create.byte(0x74);create.byte(15); // bypass push, EAX setup, CALL and cleanup
    push(create,7);create.byte(0x33);create.byte(0xc0);
    r.spans[4]=create.here();call(create,reinterpret_cast<void*>(&target_create_witness));
    const unsigned char create_cleanup[]={0x83,0xc4,0x04};create.bytes(create_cleanup,sizeof create_cleanup);
    r.spans[5]=create.here();
    const unsigned char create_end[]={0x83,0xef,0x01,0x66,0x85,0xff,0x75,0x05,0xb8,0x05,0,0xad,0x0b,0xc3};
    create.bytes(create_end,sizeof create_end);if(!create.finish())return r;
    patch::Emitter seek(64);void* seek_body=seek.here();
    push(seek,23);seek.byte(0xb8);seek.dword(0x33334444);
    r.spans[6]=seek.here();call(seek,reinterpret_cast<void*>(&target_seek_witness));r.spans[7]=seek.here();
    const unsigned char seek_end[]={0x83,0xc4,0x04,0x85,0xc0,0x75,0x05,0xb8,0x04,0,0xad,0x0b,0xc3};
    seek.bytes(seek_end,sizeof seek_end);if(!seek.finish())return r;
    patch::Emitter media(64);void* media_body=media.here();
    call(media,reinterpret_cast<void*>(&target_play_witness));call(media,create_body);call(media,seek_body);media.byte(0xc3);
    if(!media.finish())return r;
    patch::Emitter playback(128);void* playback_body=playback.here();
    playback.byte(0x57); // native dispatcher saves EDI below its six cdecl args
    for(unsigned i=6;i;--i)push(playback,playback_args[i-1]);
    r.spans[2]=playback.here();call(playback,media_body);r.spans[3]=playback.here();
    const unsigned char playback_end[]={0x83,0xc4,0x18,0x5f,0xb8,0x01,0,0,0,0xc3};
    playback.bytes(playback_end,sizeof playback_end);if(!playback.finish())return r;
    patch::Emitter publisher(128);void* publisher_body=publisher.here();r.spans[0]=publisher_body;
    // Exact 425a10 prologue, remaining three saves, and exact common epilogue.
    const unsigned char publisher_start[]={0x53,0x8b,0x5c,0x24,0x08,0x55,0x56,0x57,0x8b,0xf1};
    publisher.bytes(publisher_start,sizeof publisher_start);
    publisher.byte(0xff);publisher.byte(0x05);publisher.dword(std::uint32_t(address(target_native_counts)));
    const unsigned char gates[]={0x85,0xdb,0x74,0x0a,0x83,0xf8,0x03,0x75,0x05};publisher.bytes(gates,sizeof gates);
    call(publisher,playback_body);r.spans[1]=publisher.here();
    const unsigned char publisher_end[]={0x5f,0x5e,0x5d,0x5b,0xc2,0x04,0};publisher.bytes(publisher_end,sizeof publisher_end);
    if(!publisher.finish())return r;
    patch::Emitter wrapper(64);void* body=wrapper.here();
    wrapper.byte(0xff);wrapper.byte(0x35);wrapper.dword(std::uint32_t(address(&target_target)));
    wrapper.byte(0xa1);wrapper.dword(std::uint32_t(address(&target_mode)));
    wrapper.byte(0x8b);wrapper.byte(0x0d);wrapper.dword(std::uint32_t(address(&target_cockpit)));
    call(wrapper,publisher_body);r.publisher_return=wrapper.here();wrapper.byte(0xc3);
    if(wrapper.finish())r.body=body;
    return r;
}
static bool span_contract(unsigned kind,const void* at){
    const auto& spec=marker::kSites[kind];const auto* bytes=static_cast<const unsigned char*>(at);
    bool okay=true;
    for(unsigned i=0;i<spec.length;++i){
        if(spec.rel32_offset&&i>=spec.rel32_offset&&i<spec.rel32_offset+4)continue;
        okay=okay&&bytes[i]==spec.expected[i];
    }
    check(okay,"synthetic span matches proved native opcodes except relocated call destination");return okay;
}
static bool install_target_replay(TargetReplay& r){
    for(unsigned i=0;i<8;++i){
        if(!span_contract(marker::PublisherBegin+i,r.spans[i]))return false;
        if(!install_replay_marker(r.owned[i],marker::PublisherBegin+i,r.spans[i]))return false;
        ++r.installed;
    }
    return true;
}
static void restore_target_replay(TargetReplay& r){
    while(r.installed)check(patch::restore(r.owned[--r.installed]),"target synthetic span rollback");
}
static void target_variant(unsigned variant){
    target_target=(variant==1||variant==2)?0:0x11112222;target_mode=variant==2?2:3;
    replay_choice=variant==3?1:0;replay_result=variant==4?0:0x24681357;
    std::memset(target_native_counts,0,sizeof target_native_counts);
}
static void target_replay_checks(){
    TargetReplay r=make_target_replay();check(r.body!=nullptr,"nested target synthetic body emitted");if(!r.body)return;
    // The executable arena is monotonic. Capture every native variant before
    // one installation, then reuse those eight stubs for every hooked variant.
    Snapshot baselines[5]{};std::uint32_t native_counts[5][4]{};
    for(unsigned variant=0;variant<5;++variant){
        target_variant(variant);invoke(r.body,baselines[variant]);
        std::memcpy(native_counts[variant],target_native_counts,sizeof native_counts[variant]);
    }
    if(!install_target_replay(r)){restore_target_replay(r);return;}
    for(unsigned variant=0;variant<5;++variant){
        Snapshot hooked{};
        std::uint32_t before[8]{};for(unsigned i=0;i<8;++i)before[i]=callbacks[marker::PublisherBegin+i];
        target_variant(variant);capture_args=true;invoke(r.body,hooked);capture_args=false;compare(baselines[variant],hooked);
        const bool nested=variant!=1&&variant!=2;
        for(unsigned i=0;i<4;++i)check(target_native_counts[i]==native_counts[variant][i]&&native_counts[variant][i]==
            (i==0?1:!nested?0:i==2&&variant==3?0:1),"nested native calls and bypass execute exact count");
        for(unsigned i=0;i<8;++i)check(callbacks[marker::PublisherBegin+i]-before[i]==
            (i<2?1:!nested?0:i==4&&variant==3?0:1),"nested endpoint or common join reached exact count");
        const auto pub=marker::PublisherBegin;
        check(observed[pub][7]==target_mode&&observed[pub][6]==target_cockpit&&
              stack_args[pub][1]==target_target,"publisher entry EAX ECX and target at ESP+4 preserved");
        check(stack_args[pub][0]==address(r.publisher_return),"publisher native return PC at entry ESP preserved");
        check(observed[pub+1][3]+16==observed[pub][3],"publisher end ESP equals entry ESP minus four saved registers");
        check(hooked.regs[0]==0x98765432&&hooked.regs[1]==0x12345678&&hooked.regs[2]==0x3456789a&&
              hooked.regs[4]==0x23456789,"publisher POP x4 RET4 preserves native callee saves");
        if(nested){
            const auto play=marker::PlaybackBegin;
            check(!std::memcmp(stack_args[play],playback_args,sizeof playback_args)&&
                  !std::memcmp(target_play_args,playback_args,sizeof playback_args),"MOV6 six cdecl arguments reach marker and native callee");
            check(observed[play][3]==observed[play+1][3]&&hooked.regs[7]==1,"MOV6 end precedes cleanup24 and replays POP EDI MOV EAX 1");
            if(variant!=3){
                const auto create=marker::CreateBegin;
                check(stack_args[create][0]==7&&observed[create][7]==0&&target_create_args[0]==7&&target_create_args[1]==0,
                      "create direct-call stack argument and EAX zero preserved");
                check(observed[create+1][3]==observed[create][3]+4,"create endpoint follows caller cleanup4");
            }
            const auto seek=marker::SeekBegin;
            check(stack_args[seek][0]==23&&observed[seek][7]==0x33334444&&target_seek_args[0]==23&&target_seek_args[1]==0x33334444,
                  "seek direct-call stack argument and stream register preserved");
            check(observed[seek][3]==observed[seek+1][3]&&observed[seek+1][7]==replay_result,
                  "seek endpoint precedes cleanup and sees original result");
            check(observed[play+1][7]==(variant==4?0x0bad0004:0x24681357),"seek TEST zero and nonzero outcomes control downstream native branch");
        }
    }
    restore_target_replay(r);
}
static void input_replay_checks(unsigned variant){
    std::uint32_t control[0x500/4]{};control[0x4d8/4]=variant;control[0x4a0/4]=variant?4:0;
    patch::Emitter e(96);void* body=e.here();e.byte(0xbe);e.dword(std::uint32_t(address(control)));
    e.byte(0x33);e.byte(0xed);e.byte(0x33);e.byte(0xc0);void* before=e.here();
    const unsigned char cmp[]={0x39,0xae,0xd8,0x04,0,0,0x75,0x05,0xb8,1,0,0,0};e.bytes(cmp,sizeof cmp);
    void* after=e.here();
    const unsigned char test[]={0xf6,0x86,0xa0,0x04,0,0,4,0x74,0x05,0xb8,2,0,0,0,0xc3};e.bytes(test,sizeof test);
    const bool emitted=e.finish()!=nullptr;check(emitted,"input CMP TEST synthetic body emitted");if(!emitted)return;
    if(!span_contract(marker::InputBody,before)||!span_contract(marker::InputAfter,after))return;
    Snapshot baseline{},hooked{};invoke(body,baseline);patch::Site first{},last{};
    if(!install_replay_marker(first,marker::InputBody,before))return;
    if(!install_replay_marker(last,marker::InputAfter,after)){check(patch::restore(first),"partial input marker rollback");return;}
    const auto body_calls=callbacks[marker::InputBody],after_calls=callbacks[marker::InputAfter];
    invoke(body,hooked);compare(baseline,hooked);
    check(callbacks[marker::InputBody]==body_calls+1&&callbacks[marker::InputAfter]==after_calls+1,
          "both input partition endpoints execute exactly once");
    check(hooked.regs[7]==(variant?2:1),"input original CMP and TEST drive both branch outcomes");
    check(patch::restore(last)&&patch::restore(first),"input original spans restored");
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
            if(marker_id==marker::Input){
                fixture_call(std::uint32_t(address(mode?stubs[marker::InputBody]:continuation)),0,0,0,0,0);
                fixture_call(std::uint32_t(address(mode?stubs[marker::InputAfter]:continuation)),0,0,0,0,0);
            }
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
    std::printf("GAME PHASE BENCH trials=%u batches=%u loops_per_batch=%u marker_calls_per_loop=18 owned_bridges_per_loop=1 cpu_queries_per_enabled_loop=17 baseline_loop_us=%.6f disabled_loop_us=%.6f enabled_loop_us=%.6f disabled_added_loop_us=%.6f enabled_added_loop_us=%.6f enabled_added_per_marker_equivalent_us=%.6f scope=actual_emit_callback_core_owned_bridge_GetThreadTimes harness=paired_same_snapshot normal_tape=yes pump_metadata=valid engine_read_epoch=per_loop warmup_loops_per_batch=1 runtime_report_includes_warmup=yes game_fps=unmeasured\n",
        trials,batches,loops,number(totals[0])*scale,number(totals[1])*scale,number(totals[2])*scale,
        (number(totals[1])-number(totals[0]))*scale,(number(totals[2])-number(totals[0]))*scale,
        (number(totals[2])-number(totals[0]))*scale/18.0);
    // This is outside the measured batches. The production report supplies raw
    // GetThreadTimes query-sandwich totals/maxima and actual handler costs.
    phases::report(0);
    phases::fixture_enable(false);
    check(phases::fixture_pump_region(0,0),"fixture pump address region cleared before free");
    x3m::engine_memory::reset();
    check(VirtualFree(pump_globals,0,MEM_RELEASE)!=FALSE,"synthetic pump-global mapping released");
}
struct TargetState {std::uint64_t completed[4]{},ignored=0,reads=0;unsigned depth=0;};
static bool target_state(TargetState& state){
    const bool okay=phases::fixture_target_state(state.completed,&state.ignored,&state.reads,&state.depth);
    check(okay,"owned actual target handler state available");return okay;
}
static void admit_target_phase(void* const* stubs,unsigned mode,void* continuation){
    phases::fixture_enable(mode==2);
    for(unsigned i=0;i<=marker::Input;++i)
        fixture_call(std::uint32_t(address(mode?stubs[i]:continuation)),0,0,0,0,0);
    fixture_call(std::uint32_t(address(mode?stubs[marker::InputBody]:continuation)),0,0,0,0,0);
}
static void check_target_delta(const TargetState& before,const TargetState& after,unsigned variant,unsigned repetitions){
    const bool nested=variant!=1&&variant!=2;
    for(unsigned i=0;i<4;++i){
        const unsigned expected=i==0?(variant==2?0:repetitions):!nested?0:i==2&&variant==3?0:repetitions;
        check(after.completed[i]-before.completed[i]==expected,"actual handler matches expected target stack tokens");
    }
    check(after.ignored-before.ignored==((variant==2||variant==3)?repetitions:0),"only native untracked shared joins are ignored");
    check(after.reads==before.reads,"actual target checked reads have zero new failures");
    check(before.depth==0&&after.depth==0,"all actual target handler tokens close at native ESP");
}
static void targeted_benchmark(void* continuation,void* const* stubs){
    // The same synthetic native bodies execute in all modes. Two copies allow
    // a true unpatched baseline without patching or generating code in a clock
    // window. The hooked copy always traverses the actual production emitter.
    TargetReplay native=make_target_replay(),hooked=make_target_replay();
    check(native.body&&hooked.body,"paired targeted burst native bodies emitted");
    if(!native.body||!hooked.body)return;
    if(!install_target_replay(hooked)){restore_target_replay(hooked);return;}
    auto* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(bytes!=nullptr,"dynamic target cockpit and pump witness allocated");
    if(!bytes){restore_target_replay(hooked);return;}
    *reinterpret_cast<std::uint32_t*>(bytes+0x8adc)=1;
    *reinterpret_cast<std::uint32_t*>(bytes+0x6f3c)=std::uint32_t(address(bytes+0x9000));
    *reinterpret_cast<std::uint32_t*>(bytes+0x9000)=0;
    target_cockpit=std::uint32_t(address(bytes+0x1000));
    *reinterpret_cast<std::uint32_t*>(bytes+0x1010)=0x22224444; // raw view witness only
    *reinterpret_cast<std::uint32_t*>(bytes+0x11e0)=0x55556666;
    *reinterpret_cast<std::uint32_t*>(bytes+0x11e4)=0xabcd0002;
    phases::fixture_enable(false);const bool configured=phases::fixture_pump_region(address(bytes),0x10000);
    check(configured,"dynamic target benchmark pump region configured");
    if(!configured){VirtualFree(bytes,0,MEM_RELEASE);restore_target_replay(hooked);return;}
    phases::fixture_set_callback(nullptr);Snapshot output{};fixture_output=&output;
    // Functional checks use the real checked-read handler, including the short
    // mode3 null interval (no cockpit dereference) and rejected mode2 epilogue.
    for(unsigned variant=0;variant<5;++variant){
        x3m::engine_memory::next_frame();admit_target_phase(stubs,2,continuation);target_variant(variant);
        // Deliberately unreadable cockpit proves that rejected null/mode2
        // entries do not touch the nonnull mode3 cockpit witness fields.
        target_cockpit=(variant==1||variant==2)?1:std::uint32_t(address(bytes+0x1000));
        TargetState before{},after{};if(!target_state(before))break;
        invoke(hooked.body,output);if(target_state(after))check_target_delta(before,after,variant,1);
    }
    constexpr unsigned requests=16,batches=32,trials=3;
    static const char* const variants[]={"nonnull_mode3","null_mode3","rejected_null_mode2"};
    LARGE_INTEGER frequency{};bool valid=QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0;
    for(unsigned variant=0;variant<3&&valid;++variant){
        const bool nested=variant==0;
        target_cockpit=nested?std::uint32_t(address(bytes+0x1000)):1;
        std::uint64_t totals[3]{};
        for(unsigned trial=0;trial<trials&&valid;++trial)
            for(unsigned batch=0;batch<batches&&valid;++batch)
                for(unsigned position=0;position<3&&valid;++position){
                    const unsigned mode=(position+trial+1)%3;
                    admit_target_phase(stubs,mode,continuation);target_variant(variant);
                    TargetState before{},after{};if(mode==2&&!target_state(before)){valid=false;break;}
                    void* body=mode?hooked.body:native.body;LARGE_INTEGER start{},end{};
                    valid=QueryPerformanceCounter(&start)&&start.QuadPart>0;if(!valid)break;
                    for(unsigned request=0;request<requests;++request){
                        // Conservative first-touch validation once per request,
                        // shared by all modes, instead of perpetual cache hits.
                        x3m::engine_memory::next_frame();
                        fixture_call(std::uint32_t(address(body)),0,0,0,0,0);
                    }
                    valid=QueryPerformanceCounter(&end)&&end.QuadPart>=start.QuadPart;
                    if(valid)totals[mode]+=std::uint64_t(end.QuadPart-start.QuadPart);
                    if(mode==2&&target_state(after))check_target_delta(before,after,variant,requests);
                    for(unsigned i=0;i<4;++i)check(target_native_counts[i]==((i==0||nested)?requests:0),
                        "target benchmark executes exactly the native bodies admitted by this variant");
                }
        if(valid){
            const double scale=1e6/number(std::uint64_t(frequency.QuadPart))/double(requests*batches*trials);
            std::printf("GAME PHASE TARGET BENCH variant=%s trials=%u batches=%u requests_per_batch=%u marker_calls_per_request=%u cpu_queries_per_request=0 native_request_us=%.6f disabled_request_us=%.6f enabled_request_us=%.6f disabled_added_request_us=%.6f enabled_added_request_us=%.6f scope=actual_emit_checked_reads_ESP_tokens witness=dynamic_cockpit_and_native_stack engine_read_epoch=per_request admission_outside_clock=yes game_fps=unmeasured\n",
                variants[variant],trials,batches,requests,nested?8u:2u,number(totals[0])*scale,number(totals[1])*scale,number(totals[2])*scale,
                (number(totals[1])-number(totals[0]))*scale,(number(totals[2])-number(totals[0]))*scale);
        }
        phases::report(0);
    }
    check(valid,"paired native disabled enabled targeted burst QPC samples for all three variants");
    phases::fixture_enable(false);
    check(phases::fixture_pump_region(0,0),"target pump witness cleared before free");
    x3m::engine_memory::reset();target_cockpit=0;
    check(VirtualFree(bytes,0,MEM_RELEASE)!=FALSE,"dynamic target witness released");
    restore_target_replay(hooked);
}
extern "C" {
std::uint32_t frame_native_calls=0;
void frame_callee();void frame_callee_ret4();
}
asm(".text\n.globl _frame_callee\n_frame_callee:\n incl _frame_native_calls\n ret\n"
".globl _frame_callee_ret4\n_frame_callee_ret4:\n incl _frame_native_calls\n ret $4\n");
// One synthetic render frame: the ten real spans in the game's order with a
// two-iteration view loop whose back edge lands on the view_setup_begin span
// start (as 0x472387 does on the game's loop). Call/disp32 fields point at
// fixture objects; opcodes, lengths and relocation metadata are the contract.
struct FrameBody { void* body=nullptr;void* spans[frame_marker::Count]{};unsigned length=0; };
static std::uint32_t frame_view[0x20]{},frame_layers[0x20]{},frame_global_a=0x1111,frame_global_b=0x2222;
static FrameBody make_frame_body(){
    frame_view[0x1c/4]=std::uint32_t(address(frame_layers));
    patch::Emitter e(128);FrameBody r;r.body=e.here();
    e.byte(0xbe);e.dword(std::uint32_t(address(frame_view)));
    for(unsigned i=1;i<=4;++i)push(e,i); // the qsort arguments Views' add esp,0x10 removes
    r.spans[0]=e.here();call(e,reinterpret_cast<void*>(&frame_callee));
    r.spans[1]=e.here();call(e,reinterpret_cast<void*>(&frame_callee));
    r.spans[2]=e.here();e.byte(0xa1);e.dword(std::uint32_t(address(&frame_global_a)));
    r.spans[3]=e.here();const unsigned char views[]={0x33,0xdb,0x83,0xc4,0x10};e.bytes(views,sizeof views);
    e.byte(0xb9);e.dword(2); // two views
    r.spans[7]=e.here();e.byte(0x56);call(e,reinterpret_cast<void*>(&frame_callee_ret4));
    r.spans[8]=e.here();const unsigned char submit[]={0x8b,0x46,0x1c,0x8b,0x68,0x4c};e.bytes(submit,sizeof submit);
    r.spans[9]=e.here();e.byte(0x8b);e.byte(0x15);e.dword(std::uint32_t(address(&frame_global_b)));
    e.byte(0x49);e.byte(0x75);e.byte(static_cast<unsigned char>(-(6+6+6+1+2))); // dec ecx (1 byte); jnz view_setup_begin
    r.spans[4]=e.here();const unsigned char overlays[]={0x33,0xdb,0x39,0x5c,0x24,0x18};e.bytes(overlays,sizeof overlays);
    r.spans[5]=e.here();e.byte(0xa1);e.dword(std::uint32_t(address(&frame_global_b)));
    r.spans[6]=e.here();call(e,reinterpret_cast<void*>(&frame_callee));
    e.byte(0xc3);
    r.length=unsigned(static_cast<unsigned char*>(e.here())-static_cast<unsigned char*>(r.body));
    if(!e.finish())r.body=nullptr;
    return r;
}
// Offset of the one fixture-relocated field per span (call rel32 or absolute
// disp32); 0 = the span is byte-exact.
static constexpr unsigned frame_field[frame_marker::Count]={1,1,1,0,0,1,1,2,0,2};
static bool frame_specs(const FrameBody& r,patch::SiteSpec* specs){
    bool okay=true;
    for(unsigned k=0;k<frame_marker::Count;++k){
        specs[k]=frame_marker::kSites[k];specs[k].address=address(r.spans[k]);
        const auto* bytes=static_cast<const unsigned char*>(r.spans[k]);
        std::memcpy(specs[k].expected,bytes,specs[k].length);
        for(unsigned i=0;i<specs[k].length;++i){
            if(frame_field[k]&&i>=frame_field[k]&&i<frame_field[k]+4)continue;
            okay=okay&&bytes[i]==frame_marker::kSites[k].expected[i];
        }
    }
    check(okay,"synthetic frame spans match proved native opcodes except relocated fields");
    return okay;
}
static void frame_replay_checks(){
    FrameBody r=make_frame_body();check(r.body!=nullptr,"synthetic frame body emitted");if(!r.body)return;
    patch::SiteSpec specs[frame_marker::Count];if(!frame_specs(r,specs))return;
    unsigned char original[128];std::memcpy(original,r.body,r.length);
    Snapshot baseline{},hooked{},after{};frame_native_calls=0;invoke(r.body,baseline);
    check(frame_native_calls==5,"frame baseline executes five native calls");
    const char* status=nullptr;
    check(frame::fixture_install(specs,&status),"frame group installed on the synthetic spans");
    check(status&&!std::strcmp(status,"ok"),"frame install status ok");
    check(frame::active,"frame group active after install");
    check(std::memcmp(original,r.body,r.length)!=0,"frame spans carry the patch jumps");
    phases::fixture_set_callback(nullptr); // the real stamp handler, through the real CPU boundary
    frame::present_begin();frame::present_end(); // admits this thread and starts frame 1
    frame_native_calls=0;invoke(r.body,hooked);compare(baseline,hooked);
    check(frame_native_calls==5,"instrumented frame executes exactly the native calls");
    frame::present_begin();frame::present_end();frame::frame(1);
    frame::detail::Sample s{};
    check(frame::fixture_last_sample(&s),"closed frame sample readable by the owner thread");
    check(s.frame==1&&s.complete&&s.views==2,"scripted frame is complete with two views");
    std::uint64_t sum=0;for(unsigned i=0;i<frame::detail::phase_count;++i)sum+=s.phase_us[i];
    check(sum==s.dt_us,"phase intervals partition the frame");
    const auto* tracker=frame::fixture_tracker();
    check(tracker->order_errors==0&&tracker->clock_errors==0&&tracker->unmatched==0&&tracker->dropped==0,"scripted frame has no order, clock or unmatched errors");
    // Order: after a full frame body the tracker sits in scene_end; entering the
    // begin_scene span again is a backward core stamp, which drops the frame so
    // the next Present return restarts it without a sample.
    invoke(r.body,after);compare(baseline,after);
    {
        // Enter the body at the begin_scene span with its ESI and the four
        // pushed words in place; the remaining stamps of that pass are unmatched.
        patch::Emitter e(40);void* entry=e.here();
        e.byte(0xbe);e.dword(std::uint32_t(address(frame_view)));
        for(unsigned i=1;i<=4;++i)push(e,i);
        e.byte(0xe9);e.rel32(r.spans[2]);
        check(e.finish()!=nullptr,"begin_scene entry trampoline emitted");
        fixture_call(std::uint32_t(address(entry)),0,0,0,0,0);
    }
    check(tracker->order_errors==1&&tracker->dropped==1&&tracker->unmatched==10,"backward core stamp is an order error that drops the frame; later stamps unmatched");
    frame::present_begin();frame::present_end();frame::frame(2);
    frame::detail::Sample dropped{};frame::fixture_last_sample(&dropped);
    check(dropped.frame==1,"dropped frame produced no sample");
    check(frame::fixture_uninstall(),"frame group rollback restores every span");
    check(!std::memcmp(original,r.body,r.length),"frame spans byte-identical after rollback");
    check(!frame::active,"frame group inactive after rollback");
    phases::fixture_set_callback(&hostile_callback);
    frame_native_calls=0;invoke(r.body,after);compare(baseline,after);
    check(frame_native_calls==5,"restored frame body runs natively");
    // Byte mismatch: one corrupted opcode in the overlays span refuses the whole
    // group before any claim; no span changes.
    FrameBody c=make_frame_body();check(c.body!=nullptr,"second synthetic frame body emitted");if(!c.body)return;
    patch::SiteSpec corrupt[frame_marker::Count];if(!frame_specs(c,corrupt))return;
    unsigned char corrupted[128];std::memcpy(corrupted,c.body,c.length);
    corrupt[4].expected[0]^=1;
    check(!frame::fixture_install(corrupt,&status),"frame install refused on a byte mismatch");
    check(status&&!std::strcmp(status,"preflight_bytes"),"byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted,c.body,c.length),"no span patched after the preflight refusal");
    check(!frame::active,"frame group stays inactive after refusal");
    frame::fixture_uninstall();
    // Partial install: the second spec duplicates the first address, so its
    // claim reads the fresh jump and fails; the first site is rolled back.
    corrupt[4].expected[0]^=1;
    patch::SiteSpec partial[frame_marker::Count];std::memcpy(partial,corrupt,sizeof partial);
    partial[1]=partial[0];
    check(!frame::fixture_install(partial,&status),"frame install refused on a duplicate claim");
    check(status&&!std::strcmp(status,"bytes_mismatch"),"duplicate claim reported with the claim's own reason");
    check(!std::memcmp(corrupted,c.body,c.length),"partial install rolled back to original bytes");
    check(!frame::active,"frame group inactive after partial rollback");
    frame::fixture_uninstall();
    // Late window: after the first Present closes the window no claim is made.
    patch::close_install_window("fixture_first_present");
    check(!frame::fixture_install(corrupt,&status),"frame install refused after the install window closed");
    check(status&&!std::strcmp(status,"install_window_closed"),"late install reported as install_window_closed");
    check(!std::memcmp(corrupted,c.body,c.length),"no span touched by the late refusal");
    frame::fixture_uninstall();
}
// Pass phases (X3M_PASS_PHASES=1): the four exact effect-pass spans (plain
// copies, byte-identical to the EXE) executed once per body call at a frame
// depth of 0x90, with the operands they dereference supplied by fixture
// objects: [esp+0x28] -> a geometry object, [esp+0x74] the pass index, EBX ->
// the effect object whose first word is a vtable of >= 0x10c bytes. The body
// keeps the cdecl contract (EBX saved) so the benchmark can call it directly.
namespace pass=x3m::pass_phases;
namespace pass_marker=x3m::pass_phases::sites;
static std::uint32_t pass_effect_vtable[0x50]{},pass_effect_object[4]{},pass_geometry[8]{};
struct PassBody { void* body=nullptr;void* spans[pass_marker::Count]{};unsigned length=0; };
static PassBody make_pass_body(){
    pass_effect_object[0]=std::uint32_t(address(pass_effect_vtable));
    patch::Emitter e(96);PassBody r;r.body=e.here();
    e.byte(0x53);e.byte(0x81);e.byte(0xec);e.dword(0x90); // push ebx; sub esp,0x90
    e.byte(0xc7);e.byte(0x44);e.byte(0x24);e.byte(0x28);e.dword(std::uint32_t(address(pass_geometry))); // mov [esp+0x28],&geometry
    e.byte(0xc7);e.byte(0x44);e.byte(0x24);e.byte(0x74);e.dword(5); // mov [esp+0x74],5
    e.byte(0xbb);e.dword(std::uint32_t(address(pass_effect_object))); // mov ebx,&effect
    for(unsigned k=0;k<pass_marker::Count;++k){r.spans[k]=e.here();e.bytes(pass_marker::kSites[k].expected,pass_marker::kSites[k].length);}
    e.byte(0x81);e.byte(0xc4);e.dword(0x90);e.byte(0x5b);e.byte(0xc3); // add esp,0x90; pop ebx; ret
    r.length=unsigned(static_cast<unsigned char*>(e.here())-static_cast<unsigned char*>(r.body));
    if(!e.finish())r.body=nullptr;
    return r;
}
static void pass_specs(const PassBody& r,patch::SiteSpec* specs){
    for(unsigned k=0;k<pass_marker::Count;++k){specs[k]=pass_marker::kSites[k];specs[k].address=address(r.spans[k]);}
}
// Entry at the pass_applied span with the body's frame in place: the chain is
// entered mid-pass, so the closing stamp has no open interval (an orphan).
static void* make_pass_entry(const PassBody& r,unsigned span){
    patch::Emitter e(48);void* entry=e.here();
    e.byte(0x53);e.byte(0x81);e.byte(0xec);e.dword(0x90);
    e.byte(0xc7);e.byte(0x44);e.byte(0x24);e.byte(0x28);e.dword(std::uint32_t(address(pass_geometry)));
    e.byte(0xc7);e.byte(0x44);e.byte(0x24);e.byte(0x74);e.dword(5);
    e.byte(0xbb);e.dword(std::uint32_t(address(pass_effect_object)));
    e.byte(0xe9);e.rel32(r.spans[span]);
    return e.finish()?entry:nullptr;
}
static DWORD WINAPI pass_foreign_thread(LPVOID body){reinterpret_cast<void(*)()>(body)();return 0;}
static void pass_replay_checks(){
    PassBody r=make_pass_body();check(r.body!=nullptr,"synthetic pass body emitted");if(!r.body)return;
    patch::SiteSpec specs[pass_marker::Count];pass_specs(r,specs);
    unsigned char original[96];std::memcpy(original,r.body,r.length);
    Snapshot baseline{},hooked{},after{};invoke(r.body,baseline);
    check(baseline.regs[7]==6&&baseline.regs[5]==std::uint32_t(address(pass_effect_vtable)),"pass body baseline reads the pass index and the effect vtable");
    const char* status=nullptr;
    check(pass::fixture_install(specs,&status),"pass group installed on the synthetic spans");
    check(status&&!std::strcmp(status,"ok"),"pass install status ok");
    check(pass::active,"pass group active after install");
    check(std::memcmp(original,r.body,r.length)!=0,"pass spans carry the patch jumps");
    const auto* gate=pass::fixture_gate();const auto* accumulator=pass::fixture_accumulator();
    // Before the first frame boundary no thread is admitted: early, ignored.
    invoke(r.body,hooked);compare(baseline,hooked);
    check(gate->early.load()==pass_marker::Count&&gate->foreign.load()==0&&accumulator->passes==0,"stamps before admission are early and ignored");
    pass::frame(1,false,0); // admits this thread; no frame-phase sample yet, so the accumulation is discarded
    check(pass::fixture_dropped()==1,"frame boundary without a frame-phase sample is a dropped frame");
    invoke(r.body,hooked);compare(baseline,hooked);
    check(accumulator->passes==1&&accumulator->last==0,"one body pass counted and the interval chain closed");
    check(accumulator->orphans==0&&accumulator->clock_errors==0&&accumulator->clock_failures==0&&accumulator->unmatched==0,"scripted pass has no orphan, clock or unmatched errors");
    pass::frame(2,true,4321);
    pass::detail::Sample s{};
    check(pass::fixture_last_sample(&s),"closed pass sample readable by the owner thread");
    check(s.frame==2&&s.passes==1&&s.view_submit_us==4321,"pass sample carries the frame, the pass count and the joined view_submit");
    check(s.sum_us==s.interval_us[0]+s.interval_us[1]+s.interval_us[2],"pass intervals sum to sum_us");
    check(s.self_us==pass_marker::Count*pass::detail::dispatch_cost_ns/1000,"self cost is passes x sites x the fixture-measured dispatch cost");
    check(accumulator->passes==0,"take resets the per-frame accumulation");
    // A stamp from another thread is foreign, counted and ignored.
    HANDLE thread=CreateThread(nullptr,0,&pass_foreign_thread,r.body,0,nullptr);
    check(thread!=nullptr,"foreign thread started");
    if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}
    check(gate->foreign.load()==pass_marker::Count&&accumulator->passes==0,"foreign-thread stamps are counted and ignored");
    // Entering the chain at pass_applied: the closing stamp is an orphan, the
    // draw and EndPass intervals still accumulate and the pass is counted.
    void* entry=make_pass_entry(r,pass_marker::PassApplied);check(entry!=nullptr,"pass_applied entry trampoline emitted");
    if(entry){
        invoke(entry,after);compare(baseline,after);
        check(accumulator->orphans==1&&accumulator->passes==1&&accumulator->last==0,"mid-pass entry is one orphan, still one pass");
    }
    pass::frame(3,true,0);
    check(pass::fixture_uninstall(),"pass group rollback restores every span");
    check(!std::memcmp(original,r.body,r.length),"pass spans byte-identical after rollback");
    check(!pass::active,"pass group inactive after rollback");
    invoke(r.body,after);compare(baseline,after);
    // Byte mismatch: one corrupted opcode refuses the whole group before any claim.
    PassBody c=make_pass_body();check(c.body!=nullptr,"second synthetic pass body emitted");if(!c.body)return;
    patch::SiteSpec corrupt[pass_marker::Count];pass_specs(c,corrupt);
    unsigned char corrupted[96];std::memcpy(corrupted,c.body,c.length);
    corrupt[2].expected[0]^=1;
    check(!pass::fixture_install(corrupt,&status),"pass install refused on a byte mismatch");
    check(status&&!std::strcmp(status,"preflight_bytes"),"pass byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted,c.body,c.length),"no pass span patched after the preflight refusal");
    check(!pass::active,"pass group stays inactive after refusal");
    pass::fixture_uninstall();
    // Partial install: the second spec duplicates the first address, so its
    // claim reads the fresh jump and fails; the first site is rolled back.
    corrupt[2].expected[0]^=1;
    patch::SiteSpec partial[pass_marker::Count];std::memcpy(partial,corrupt,sizeof partial);
    partial[1]=partial[0];
    check(!pass::fixture_install(partial,&status),"pass install refused on a duplicate claim");
    check(status&&!std::strcmp(status,"bytes_mismatch"),"pass duplicate claim reported with the claim's own reason");
    check(!std::memcmp(corrupted,c.body,c.length),"pass partial install rolled back to original bytes");
    check(!pass::active,"pass group inactive after partial rollback");
    pass::fixture_uninstall();
}
static void pass_late_window_checks(){ // after the frame checks closed the install window
    PassBody r=make_pass_body();check(r.body!=nullptr,"late-window pass body emitted");if(!r.body)return;
    patch::SiteSpec specs[pass_marker::Count];pass_specs(r,specs);
    unsigned char original[96];std::memcpy(original,r.body,r.length);
    const char* status=nullptr;
    check(!pass::fixture_install(specs,&status),"pass install refused after the install window closed");
    check(status&&!std::strcmp(status,"install_window_closed"),"late pass install reported as install_window_closed");
    check(!std::memcmp(original,r.body,r.length),"no pass span touched by the late refusal");
    pass::fixture_uninstall();
}
// Per-dispatch cost of the lean stub: the body (four spans) called directly,
// unhooked against hooked, best of `trials`; the difference over four
// dispatches is the stub envelope + owner check + QPC + accumulate.
static void pass_benchmark(){
    constexpr unsigned loops=20000,trials=7;
    PassBody r=make_pass_body();check(r.body!=nullptr,"benchmark pass body emitted");if(!r.body)return;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"pass benchmark QPC frequency");
    const auto body=reinterpret_cast<void(*)()>(r.body);
    const auto timed=[&](std::uint64_t& best){
        best=~std::uint64_t(0);
        for(unsigned t=0;t<trials;++t){
            LARGE_INTEGER start{},end{};
            if(!QueryPerformanceCounter(&start))return false;
            for(unsigned i=0;i<loops;++i)body();
            if(!QueryPerformanceCounter(&end)||end.QuadPart<start.QuadPart)return false;
            const auto ticks=std::uint64_t(end.QuadPart-start.QuadPart);
            if(ticks<best)best=ticks;
        }
        return true;
    };
    std::uint64_t baseline=0,hooked=0;
    check(timed(baseline),"pass benchmark baseline timed");
    patch::SiteSpec specs[pass_marker::Count];pass_specs(r,specs);
    const char* status=nullptr;
    check(pass::fixture_install(specs,&status),"pass benchmark group installed");
    pass::frame(1,false,0);
    check(timed(hooked),"pass benchmark hooked timed");
    pass::frame(2,true,0);
    pass::detail::Sample s{};
    check(pass::fixture_last_sample(&s)&&s.passes==loops*trials,"hooked benchmark loop counted every pass");
    check(pass::fixture_uninstall(),"pass benchmark group rolled back");
    const double ns_per_tick=1e9/number(std::uint64_t(frequency.QuadPart));
    const double baseline_ns=number(baseline)*ns_per_tick/loops,hooked_ns=number(hooked)*ns_per_tick/loops;
    const double dispatch_ns=(hooked_ns-baseline_ns)/pass_marker::Count;
    const double busy_us=dispatch_ns*4024/1000; // ~1,006 passes x 4 stamps per busy frame (effect-pass-loop.md section 4)
    std::printf("PASS PHASE BENCH loops=%u trials=%u baseline_ns_per_loop=%.1f hooked_ns_per_loop=%.1f dispatch_ns=%.1f implied_busy_frame_us=%.0f budget_us=1500 within_budget=%u documented_dispatch_ns=%llu\n",
        loops,trials,baseline_ns,hooked_ns,dispatch_ns,busy_us,unsigned(busy_us<=1500.0),static_cast<unsigned long long>(pass::detail::dispatch_cost_ns));
    check(busy_us<=1500.0,"lean stub within the 1.5 ms busy-frame budget");
    // The documented constant behind self_p50_us must stay within a factor of
    // two of the measurement; a drift beyond that is a stale ledger, not noise.
    const double documented=number(pass::detail::dispatch_cost_ns);
    check(dispatch_ns>0&&documented>=dispatch_ns*0.5&&documented<=dispatch_ns*2.0,"documented dispatch cost within 2x of the measured cost");
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
    target_replay_checks();
    input_replay_checks(0);input_replay_checks(1);
    benchmark(continuation,stubs);
    targeted_benchmark(continuation,stubs);
    pass_replay_checks();pass_benchmark();
    frame_replay_checks(); // closes the install window
    pass_late_window_checks();
    std::printf("GAME PHASE CPU stubs=%u frame_sites=%u pass_sites=%u replay_cases=13 actual_target_handler_cases=5 frame_cases=4 pass_cases=5 arena_used=%u checks=%u failures=%u\n",unsigned(marker::Count),unsigned(frame_marker::Count),unsigned(pass_marker::Count),patch::arena_used(),checks,failures);
    return failures?1:0;
}
