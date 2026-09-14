#include "game_phases.h"
#include "game_phases_core.h"
#include "game_phase_sites.h"
#include "cpu_state.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "telemetry.h"
#include "sampling_profiler.h"
#include "capture.h"
#include <atomic>
#include <cstdio>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
namespace x3m::game_phases {
namespace {
std::atomic<bool> active{false};
std::atomic<DWORD> owner_thread{0},invalidation_epoch{0};
std::atomic<unsigned> foreign_hits{0},suppressed{0};
bool initialized=false,reporting=false;
DWORD seen_epoch=0;
detail::Core core;
engine_patch::Site patches[sites::Count];
unsigned site_count=sites::PhaseCount; // sites::Count when X3M_AUDIO_SITES=1 adds the audio witnesses
// Audio-path witnesses: main-thread writer (every site is on the main thread,
// docs/reverse-engineering/voice-startup-sequence.md section 1), any-thread
// reader. Plain atomics, no ownership gate: the two pre-loop manager sites
// run before LoopSetup establishes the owner. Indexed by site index; only
// Services (403b04, the sixth 498370 call site) and the audio indices are used.
bool audio_enabled=false;
struct AudioCounter { std::atomic<std::uint32_t> total{0},frame{0},last{0}; };
AudioCounter audio_counts[sites::Count];
std::atomic<std::uint32_t> audio_loops{0},audio_poll_ok{0},audio_poll_pending{0},audio_poll_eos{0},audio_poll_other{0};
std::atomic<std::uint32_t> audio_last_poll{0},audio_last_update{0},audio_last_setstate{0},audio_last_pause{0};
bool audio_poll_armed=false; // main thread only: PollCall seen, its PollAfter pending
detail::Metric handler_cost,query_cost,bridge_cost;
std::uint64_t read_failures=0,cpu_failures=0,publisher_entries=0,publisher_filtered=0;
#ifdef X3M_GAME_PHASE_FIXTURE
void (__cdecl* fixture_callback)(unsigned,const std::uint32_t*)=nullptr;
std::uintptr_t pump_active_address=0,pump_flags_address=0;
#else
constexpr std::uintptr_t pump_active_address=0x608adc,pump_flags_address=0x606f3c;
#endif
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
std::uint64_t qpc() noexcept {LARGE_INTEGER v{};return QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0;}
std::uint64_t ticks(FILETIME v) noexcept {return (std::uint64_t(v.dwHighDateTime)<<32)|v.dwLowDateTime;}
detail::Stamp stamp(bool cpu) noexcept {
    detail::Stamp s{};s.qpc=qpc();
    if(cpu&&s.qpc){
        s.query_begin=s.qpc;
        FILETIME creation{},exit{},kernel{},user{};
        s.cpu=GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user)!=FALSE;
        s.query_end=qpc();
        if(!s.query_end||s.query_end<s.qpc)s.cpu=false;
        if(s.cpu){s.user=ticks(user);s.kernel=ticks(kernel);}else ++cpu_failures;
        query_cost.add(detail::Stamp{s.qpc},detail::Stamp{s.query_end});
    }
    return s;
}
void synchronize() noexcept {
    const DWORD epoch=invalidation_epoch.load(std::memory_order_acquire);
    if(epoch!=seen_epoch){core.invalidate();seen_epoch=epoch;}
}
bool owner(unsigned index) noexcept {
    const DWORD thread=GetCurrentThreadId();DWORD expected=0;
    if(index==sites::LoopSetup)owner_thread.compare_exchange_strong(expected,thread,std::memory_order_acq_rel);
    if(owner_thread.load(std::memory_order_acquire)!=thread){foreign_hits.fetch_add(1,std::memory_order_relaxed);return false;}
    if(reporting){suppressed.fetch_add(1,std::memory_order_relaxed);invalidation_epoch.fetch_add(1,std::memory_order_release);return false;}
    synchronize();return true;
}
bool read_word(std::uintptr_t at,std::uint32_t& value) noexcept {
    if(at&&engine_memory::read(at,&value,sizeof value))return true;
    ++read_failures;core.invalidate();return false;
}
void audio_hit(unsigned index,const std::uint32_t* regs) noexcept {
    auto& c=audio_counts[index];
    c.total.fetch_add(1,std::memory_order_relaxed);c.frame.fetch_add(1,std::memory_order_relaxed);
    const std::uint32_t eax=regs[7]; // PUSHAD: EAX is the last pushed
    switch(index){
    case sites::AudioSetStateAfter:audio_last_setstate.store(eax,std::memory_order_relaxed);break;
    case sites::AudioPauseAfter:audio_last_pause.store(eax,std::memory_order_relaxed);break;
    case sites::AudioPollCall:audio_poll_armed=true;break;
    case sites::AudioPollAfter:
        // 4d0774 is also reached from the state!=2 branch at 4d0758; only a
        // hit paired with the call setup carries CompletionStatus in EAX.
        if(!audio_poll_armed)break;
        audio_poll_armed=false;audio_last_poll.store(eax,std::memory_order_relaxed);
        (eax==0?audio_poll_ok:eax==0x40001?audio_poll_pending:eax==0x40003?audio_poll_eos:audio_poll_other).fetch_add(1,std::memory_order_relaxed);
        break;
    case sites::AudioUpdateAfter:audio_last_update.store(eax,std::memory_order_relaxed);break;
    default:break;
    }
}
void audio_rollover() noexcept { // LoopSetup: the previous main-loop frame is complete
    audio_loops.fetch_add(1,std::memory_order_relaxed);
    for(auto& c:audio_counts){c.last.store(c.frame.load(std::memory_order_relaxed),std::memory_order_relaxed);c.frame.store(0,std::memory_order_relaxed);}
}
void handle(unsigned index,const std::uint32_t* regs) noexcept {
    if(!active.load(std::memory_order_acquire))return;
    if(audio_enabled){
        if(index>=sites::PhaseCount||index==sites::Services)audio_hit(index,regs);
        else if(index==sites::LoopSetup)audio_rollover();
        if(index>=sites::PhaseCount)return;
    }
    if(!owner(index))return;
    const auto at=stamp(index<detail::phase_count||index==sites::InputBody||index==sites::InputAfter);
    // A lifecycle event between admission and the clock must revoke the old
    // token before this timestamp is applied. Events after this acquire are
    // later than the recorded boundary and revoke it at the next endpoint.
    synchronize();
    if(index<=sites::Exit){
        core.boundary(index,at);
        if(index==sites::Pump){
            std::uint32_t flags=0;detail::Pump p;
            p.valid=engine_memory::read(pump_active_address,&p.active,4)&&engine_memory::read(pump_flags_address,&flags,4)
                &&flags&&engine_memory::read(flags,&p.flags,4);
            if(!p.valid)++read_failures;
            core.pump=p;
        }
    }
    else {
        // PUSHAD layout: EDI,ESI,EBP,saved ESP,EBX,EDX,ECX,EAX;
        // saved ESP points at PUSHFD, so native ESP is four bytes higher.
        const std::uintptr_t esp=std::uintptr_t(regs[3])+4;
        std::uint32_t word=0;
        switch(index){
        case sites::DelayedBegin:
            if(regs[7]!=3){++read_failures;core.invalidate();break;}
            if(!read_word(esp,word))break;
            core.begin(0,at,{regs[6],word,3});break;
        case sites::DelayedEnd:core.end(0,at,regs[7]);break;
        case sites::AcquisitionBegin:
            if(regs[2]!=0){++read_failures;core.invalidate();break;}
            core.begin(1,at,{regs[1],regs[4],2});break;
        case sites::AcquisitionEnd:core.end(1,at,regs[7]);break;
        case sites::ColdBegin:core.begin(2,at,core.request);break;
        case sites::ColdEnd:core.end(2,at,regs[7]);break;
        case sites::PresentBegin:
            if(read_word(esp,word))core.begin(3,at,core.request,word);
            break;
        case sites::PresentEnd:core.end(3,at,regs[7]);break;
        case sites::InputBody:core.input_boundary(1,at);break;
        case sites::InputAfter:core.input_boundary(2,at);break;
        case sites::PublisherBegin: {
            ++publisher_entries;
            if(regs[7]!=3||!core.targeted()){++publisher_filtered;break;}
            detail::Witness w;
            if(esp<16||esp>UINT32_MAX-4){++read_failures;core.invalidate();break;}
            if(!read_word(esp,w.caller)||!read_word(esp+4,word))break;
            const auto cockpit=std::uintptr_t(regs[6]);
            // Mode3 dereferences cockpit only for a nonnull target. A null
            // request returns through the shared epilogue without publication.
            if(word){
                if(!cockpit||cockpit>UINT32_MAX-0x1e8){++read_failures;core.invalidate();break;}
                if(!read_word(cockpit+0x1e0,w.previous_target)||!read_word(cockpit+0x1e4,w.previous_mode)
                    ||!read_word(cockpit+0x10,w.view))break;
                w.previous_mode&=0xffff;
            }
            w.end_esp=esp-16;core.target_begin(4,at,{regs[6],word,3},w);break;
        }
        case sites::PublisherEnd:core.target_end(4,at,esp,regs[7]);break;
        case sites::PlaybackBegin: {
            if(!core.targeted())break;
            detail::Witness w;w.end_esp=esp;
            if(esp>UINT32_MAX-sizeof(w.args)||!engine_memory::read(esp,w.args,sizeof(w.args))){++read_failures;core.invalidate();break;}
            core.target_begin(5,at,core.request_for(4),w);break;
        }
        case sites::PlaybackEnd:core.target_end(5,at,esp,regs[7]);break;
        case sites::CreateBegin: {
            if(!core.targeted()||!core.within(5))break;
            detail::Witness w;
            if(esp>UINT32_MAX-4){++read_failures;core.invalidate();break;}
            if(!read_word(esp,w.args[0]))break;
            w.args[1]=regs[7];w.end_esp=esp+4;core.target_begin(6,at,core.request_for(5),w);break;
        }
        case sites::CreateEnd:core.target_end(6,at,esp,regs[7]);break;
        case sites::SeekBegin: {
            if(!core.targeted()||!core.within(5))break;
            detail::Witness w;
            if(!read_word(esp,w.args[0]))break;
            w.args[1]=regs[7];w.end_esp=esp;core.target_begin(7,at,core.request_for(5),w);break;
        }
        case sites::SeekEnd:core.target_end(7,at,esp,regs[7]);break;
        default:++read_failures;core.invalidate();break;
        }
    }
    handler_cost.add(at,detail::Stamp{qpc()});
}
void* emit(unsigned index,void*** next_out);
bool install_group(const char*& status) {
    active.store(false,std::memory_order_release);
    if(!engine_patch::install_window_open()){status="install_window_closed";return false;}
    for(unsigned i=0;i<site_count;++i)
        if(!engine_patch::verify_bytes(sites::kSites[i].address,sites::kSites[i].expected,sites::kSites[i].length)){
            status="preflight_bytes";return false;
        }
    for(unsigned i=0;i<site_count;++i){
        if(engine_patch::claim(patches[i],sites::kSites[i])){
            void** next=nullptr;void* stub=emit(i,&next);
            if(stub&&next&&engine_patch::store_pointer(next,*patches[i].entry)&&engine_patch::push_front(patches[i],stub))continue;
            status="stub_chain_failed";
        }else status="claim_failed";
        bool restored=true;
        for(unsigned j=site_count;j-->0;)if(patches[j].patched_in&&!engine_patch::restore(patches[j]))restored=false;
        if(!restored)status="rollback_failed_inert";
        return false;
    }
    // The audio flag is published before activation so an audio-site hit
    // never reaches the phase switch default (which would invalidate the core).
    audio_enabled=site_count>sites::PhaseCount;
    status="ok";active.store(true,std::memory_order_release);return true;
}
}
}

extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_game_phase_enter(unsigned index,const std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr=0x1f80;asm volatile("ldmxcsr %0" :: "m"(mxcsr):"memory");
#ifdef X3M_GAME_PHASE_FIXTURE
    if(x3m::game_phases::fixture_callback){x3m::game_phases::fixture_callback(index,regs);return;}
#endif
    x3m::game_phases::handle(index,regs);
}
namespace x3m::game_phases {
namespace {
void* emit(unsigned index,void*** next_out) {
    engine_patch::Emitter e(192);if(!e.ok())return nullptr;void* start=e.here();
    e.byte(0x9c);e.byte(0x60);e.byte(0xfc);
    e.byte(0x81);e.byte(0xec);e.dword(0x80);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x8d);e.byte(0x84);e.byte(0x24);e.dword(0x80);e.byte(0x50);
    e.byte(0x68);e.dword(index);e.byte(0xe8);e.rel32(reinterpret_cast<const void*>(&x3m_game_phase_enter));
    e.byte(0x83);e.byte(0xc4);e.byte(8);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x61);e.byte(0x9d);
    const auto next=(reinterpret_cast<std::uintptr_t>(e.here())+9)&~std::uintptr_t(3);
    e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));
    while(e.ok()&&reinterpret_cast<std::uintptr_t>(e.here())<next)e.byte(0xcc);
    *next_out=reinterpret_cast<void**>(next);e.dword(0);return e.finish()?start:nullptr;
}
}
bool initialize() {
    ErrorGuard error;
    if(initialized)return active.load(std::memory_order_acquire);
    initialized=true;wchar_t value[4]{};
    const bool wanted=GetEnvironmentVariableW(L"X3M_GAME_PHASES",value,4)==1&&value[0]==L'1';
    if(!wanted)return false;
    wchar_t audio[4]{};
    const bool audio_wanted=GetEnvironmentVariableW(L"X3M_AUDIO_SITES",audio,4)==1&&audio[0]==L'1';
    site_count=audio_wanted?sites::Count:sites::PhaseCount;
    const char* status="telemetry_off";
    if(telemetry::enabled()){
        core.frequency=telemetry::frequency();
        if(!core.frequency)status="clock_unavailable";
        else if(!object_trace::executable_verified())status="executable_unverified";
        else install_group(status);
    }
    audio_enabled=audio_enabled&&active.load(std::memory_order_acquire);
    // Timed emission while frame progression is stopped: the sampler thread
    // (X3M_PROFILE=1) runs the callback every 2 s outside its suspended window.
    if(audio_enabled)sampling_profiler::set_periodic([](std::uint64_t qpc_stamp){audio_report("timed",qpc_stamp);},2);
    log("game_phase_mode requested=1 enabled=%u status=%s sites=%u audio_sites_requested=%u audio_sites=%u owner=main_loop frame_threshold_ms=50 call_threshold_ms=10 tape=96 nesting=8 first=4 recent=4 qpc_frequency=%llu cpu_clock=GetThreadTimes",
        unsigned(active.load()),status,site_count,unsigned(audio_wanted),unsigned(audio_enabled),core.frequency);
    for(unsigned i=0;i<sites::Count;++i)log("game_phase_site index=%u address=%08lx length=%u rel32=%u patched=%u status=%s",i,
        static_cast<unsigned long>(sites::kSites[i].address),sites::kSites[i].length,sites::kSites[i].rel32_offset,unsigned(patches[i].patched_in),patches[i].status);
    return active.load(std::memory_order_acquire);
}
void present_endpoint(std::uintptr_t raw,std::uint64_t device,std::uint64_t reset,std::uint64_t frame,bool captured,std::uint64_t endpoint,std::uint32_t result) noexcept {
    if(!active.load(std::memory_order_acquire))return;
    ErrorGuard error;if(!owner(sites::PresentEnd))return;
    const auto sample=stamp(true);synchronize();
    core.bridge(detail::Present{device,reset,frame,endpoint,raw,result,captured},sample);
    bridge_cost.add(sample,detail::Stamp{qpc()});
}
void invalidate_device() noexcept {
    if(!active.load(std::memory_order_acquire))return;
    invalidation_epoch.fetch_add(1,std::memory_order_release);
}
namespace {
detail::LoadingPhases loading_phases; // Present path only (capture mutex)
std::uint64_t loading_frequency=0;
constexpr const char* loading_phase_names[detail::LoadingPhases::NameCount]={"menu_shown","save_load_begin","save_load_complete"};
constexpr unsigned loading_stall_seconds=3; // splash gaps stay under 2 s, load stalls above 5 s (runs 39-46)
}
void loading_phase_present(std::uint64_t device,std::uint64_t reset,std::uint64_t frame) noexcept {
    if(loading_phases.emitted==(1u<<detail::LoadingPhases::NameCount)-1)return; // all markers written: no clock
    ErrorGuard error;
    if(!loading_frequency){
        LARGE_INTEGER f{};
        if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return;
        loading_frequency=std::uint64_t(f.QuadPart);loading_phases.stall_ticks=loading_frequency*loading_stall_seconds;
    }
    const auto now=qpc();if(!now)return;
    detail::LoadingPhases::Marker markers[2];
    const unsigned count=loading_phases.present(device,reset,frame,now,markers);
    for(unsigned i=0;i<count;++i){
        const auto& m=markers[i];
        const auto origin=static_cast<std::uint64_t>(dll_load_qpc);
        log("loading_phase name=%s frame=%llu elapsed_ms=%llu stall_ms=%llu device=%llu qpc=%llu",loading_phase_names[m.name],m.frame,
            m.qpc>=origin?(m.qpc-origin)*1000ull/loading_frequency:0,m.stall*1000ull/loading_frequency,device,m.qpc);
    }
}
bool audio_active() noexcept {return audio_enabled&&active.load(std::memory_order_acquire);}
void audio_report(const char* scope,std::uint64_t qpc_stamp) {
    if(!audio_active())return;
    ErrorGuard error;
    // One line, integers only: cumulative total, count inside the current
    // (possibly stuck) main-loop frame and the last completed frame's count.
    char sites_text[sites::Count*48];unsigned used=0;
    const auto add=[&](const char* name,const AudioCounter& c){
        const int n=std::snprintf(sites_text+used,sizeof sites_text-used,"%s%s=%u/%u/%u",used?" ":"",name,
            c.total.load(std::memory_order_relaxed),c.frame.load(std::memory_order_relaxed),c.last.load(std::memory_order_relaxed));
        if(n>0&&unsigned(n)<sizeof sites_text-used)used+=unsigned(n);
    };
    add("manager_loop",audio_counts[sites::Services]);
    for(unsigned i=sites::PhaseCount;i<sites::Count;++i)add(sites::kSites[i].name+sizeof("game_phase_audio_")-1,audio_counts[i]);
    log("game_phase_audio scope=%s qpc=%llu loops=%u poll_ok=%u poll_pending=%u poll_eos=%u poll_other=%u last_poll_hr=%08x last_update_hr=%08x last_setstate_hr=%08x last_pause_hr=%08x %s",
        scope,qpc_stamp,audio_loops.load(std::memory_order_relaxed),audio_poll_ok.load(std::memory_order_relaxed),audio_poll_pending.load(std::memory_order_relaxed),
        audio_poll_eos.load(std::memory_order_relaxed),audio_poll_other.load(std::memory_order_relaxed),audio_last_poll.load(std::memory_order_relaxed),
        audio_last_update.load(std::memory_order_relaxed),audio_last_setstate.load(std::memory_order_relaxed),audio_last_pause.load(std::memory_order_relaxed),sites_text);
}
void report(std::uint64_t reporting_frame) {
    if(!active.load(std::memory_order_acquire)||GetCurrentThreadId()!=owner_thread.load(std::memory_order_acquire)||reporting)return;
    ErrorGuard error;synchronize();reporting=true;
    log("game_phase_window frame=%llu owner=%lu loops=%llu frames=%llu max_ticks=%llu max_begin=%llu max_end=%llu max_device=%llu max_frame=%llu invalidated=%llu unmatched=%llu overflow=%llu order_errors=%llu clock_errors=%llu foreign=%u suppressed=%u read_failures=%llu cpu_failures=%llu slow_frames=%llu retained_frames=%u slow_calls=%llu retained_calls=%u ignored_joins=%llu publisher_entries=%llu publisher_filtered=%llu",
        reporting_frame,owner_thread.load(),core.loop,core.frame_count,core.frame_max,core.frame_max_begin,core.frame_max_end,core.frame_max_device,core.frame_max_id,
        core.invalidated,core.unmatched,core.overflow,core.order_errors,core.clock_errors,foreign_hits.exchange(0),suppressed.exchange(0),read_failures,cpu_failures,
        core.slow_frames.count,core.slow_frames.first_used+core.slow_frames.recent_used,core.slow_calls.count,core.slow_calls.first_used+core.slow_calls.recent_used,core.ignored_joins,publisher_entries,publisher_filtered);
    audio_report("window",qpc());
    const auto metric=[](const char* category,unsigned index,const detail::Metric& m){
        if(m.count)log("game_phase_metric category=%s index=%u count=%llu ticks=%llu max_ticks=%llu max_begin=%llu max_end=%llu cpu_valid=%llu user_100ns=%llu kernel_100ns=%llu",category,index,m.count,m.total,m.maximum,m.begin,m.end,m.cpu_valid,m.user,m.kernel);
    };
    for(unsigned i=0;i<detail::phase_count;++i)metric("phase",i,core.phases[i]);
    for(unsigned i=0;i<detail::call_count;++i)metric("native_call",i,core.calls[i]);
    for(unsigned i=0;i<3;++i)metric("input_part",i,core.input_parts[i]);
    metric("handler",0,handler_cost);metric("cpu_query",0,query_cost);metric("present_bridge",0,bridge_cost);
    const auto print_frame=[](const detail::Frame& f){
        const auto elapsed=f.current.qpc-f.previous.qpc;
        log("game_phase_slow_frame device=%llu reset=%llu previous_frame=%llu frame=%llu qpc_begin=%llu qpc_end=%llu previous_capture=%u capture=%u result=%08x dispatch_begin=%llu dispatch_end=%llu covered_ticks=%llu residual_ticks=%llu segments=%u overflow=%u",
            f.current.device,f.current.reset,f.previous.frame,f.current.frame,f.previous.qpc,f.current.qpc,unsigned(f.previous.captured),unsigned(f.current.captured),f.current.result,
            f.dispatch_begin,f.dispatch_end,f.covered,elapsed>=f.covered?elapsed-f.covered:0,f.used,unsigned(f.overflow));
        for(unsigned i=0;i<f.used;++i){const auto& s=f.segments[i];
            const bool cpu=s.begin.cpu&&s.end.cpu&&s.end.user>=s.begin.user&&s.end.kernel>=s.begin.kernel;
            log("game_phase_segment device=%llu frame=%llu index=%u phase=%u input_part=%u loop=%llu qpc_begin=%llu qpc_end=%llu cpu_valid=%u user_100ns=%llu kernel_100ns=%llu begin_query_begin=%llu begin_query_end=%llu end_query_begin=%llu end_query_end=%llu requested_target_begin=%08x requested_target_end=%08x requested_mode_begin=%u requested_mode_end=%u pump_valid=%u pump_active=%u pump_flags=%08x",
                f.current.device,f.current.frame,i,s.phase,s.input_part,s.loop,s.begin.qpc,s.end.qpc,unsigned(cpu),cpu?s.end.user-s.begin.user:0,cpu?s.end.kernel-s.begin.kernel:0,
                s.begin.query_begin,s.begin.query_end,s.end.query_begin,s.end.query_end,s.request_begin.target,s.request_end.target,s.request_begin.mode,s.request_end.mode,unsigned(s.pump.valid),s.pump.active,s.pump.flags);
        }
    };
    for(unsigned i=0;i<core.slow_frames.first_used;++i)print_frame(core.slow_frames.first[i]);
    for(unsigned i=0;i<core.slow_frames.recent_used;++i)print_frame(core.slow_frames.recent[((core.slow_frames.recent_used==4?core.slow_frames.next:0)+i)&3]);
    const auto print_call=[](const detail::Call& c){
        log("%s kind=%u qpc_begin=%llu qpc_end=%llu cockpit=%08x requested_target=%08x requested_mode=%u %s=%08x device=%llu reset=%llu frame=%llu endpoint_qpc=%llu loop=%llu phase=%u caller=%08x previous_target=%08x previous_mode=%u view=%08x end_esp=%08lx children_ticks=%llu exclusive_ticks=%llu first=%u anchor=%u anchor_capture=%u arg0=%08x arg1=%08x arg2=%08x arg3=%08x arg4=%08x arg5=%08x",
            c.first?"game_phase_call_sample":"game_phase_slow_call",c.kind,c.begin.qpc,c.end.qpc,c.request.cockpit,c.request.target,c.request.mode,c.kind>=4?"endpoint_eax":"result",c.result,c.present.device,c.present.reset,c.present.frame,c.present.qpc,c.loop,c.phase,c.witness.caller,c.witness.previous_target,c.witness.previous_mode,c.witness.view,
            static_cast<unsigned long>(c.witness.end_esp),c.children,(c.end.qpc-c.begin.qpc)>=c.children?(c.end.qpc-c.begin.qpc)-c.children:0,unsigned(c.first),unsigned(c.kind>=4&&c.present.qpc&&c.present.device&&c.present.raw),unsigned(c.present.captured),
            c.witness.args[0],c.witness.args[1],c.witness.args[2],c.witness.args[3],c.witness.args[4],c.witness.args[5]);
    };
    for(unsigned i=0;i<core.slow_calls.first_used;++i)print_call(core.slow_calls.first[i]);
    for(unsigned i=0;i<core.slow_calls.recent_used;++i)print_call(core.slow_calls.recent[((core.slow_calls.recent_used==4?core.slow_calls.next:0)+i)&3]);
    for(unsigned i=4;i<detail::call_count;++i)if(core.pending_first&(1u<<i))print_call(core.first_calls[i-4]);
    core.clear_window();handler_cost={};query_cost={};bridge_cost={};read_failures=cpu_failures=publisher_entries=publisher_filtered=0;reporting=false;
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_pump_region(std::uintptr_t base,std::uint32_t bytes) noexcept {
    if(active.load(std::memory_order_acquire))return false;
    if(!base&&!bytes){pump_active_address=pump_flags_address=0;return true;}
    // Include all three DWORDs, including the pointed-to flags at +0x9000.
    // Address arithmetic must remain representable in the actual x86 ABI.
    if(!base||(base&3)||bytes<0x9004||base>UINT32_MAX-bytes)return false;
    pump_active_address=base+0x8adc;pump_flags_address=base+0x6f3c;
    return true;
}
bool fixture_target_state(std::uint64_t* completed4,std::uint64_t* ignored,std::uint64_t* reads,unsigned* depth) {
    if(!completed4||!ignored||!reads||!depth||owner_thread.load()!=GetCurrentThreadId())return false;
    for(unsigned i=0;i<4;++i)completed4[i]=core.calls[i+4].count;
    *ignored=core.ignored_joins;*reads=read_failures;*depth=core.depth;return true;
}
void* fixture_emit(unsigned index,void*** next){return index<sites::Count?emit(index,next):nullptr;}
void fixture_set_callback(void (__cdecl* callback)(unsigned,const std::uint32_t*)){fixture_callback=callback;}
void fixture_enable(bool enabled){
    active.store(false);core={};owner_thread.store(0);invalidation_epoch.store(0);seen_epoch=0;
    LARGE_INTEGER f{};if(QueryPerformanceFrequency(&f)&&f.QuadPart>0)core.frequency=std::uint64_t(f.QuadPart);
    active.store(enabled&&core.frequency);
}
#endif
}
