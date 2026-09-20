#include "media_cue.h"
#include "media_cue_sites.h"
#include "stamp_install.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "telemetry.h"
#include "capture.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
static_assert(sizeof(x3m::media_cue::EnterFrame)==0xa4,"gate stub frame layout");
static_assert(sizeof(x3m::media_cue::ReturnFrame)==0x98,"return trampoline frame layout");
namespace x3m::media_cue {
std::atomic<bool> active{false};
static std::atomic<OwnedEligibility> owned_eligibility{nullptr};
void set_owned_eligibility(OwnedEligibility predicate) noexcept {owned_eligibility.store(predicate,std::memory_order_release);}
namespace {
std::atomic<bool> installed{false};
bool initialized=false,trace_on=false,cache_on=false;
std::uint64_t frequency=0,retry_ticks=0;
constexpr unsigned default_retry_s=30,max_retry_s=3600;
detail::Addresses addresses{sites::kQueryReturn,sites::kSavegameReturn,sites::kScriptReturn,sites::kSpeechReturn,
                            sites::kHelperReturn,sites::kSelectorReturn,sites::kSelectorKind,
                            sites::kVideoBlitBegin,sites::kVideoBlitEnd};
detail::Gate gate;
detail::NegativeCache cache;
detail::PendingStack pending;
detail::TraceRing ring;
detail::RateLimit limit;        // the drained outcome lines
detail::RateLimit enter_limit;  // the synchronous entry lines
detail::Window window;
detail::VideoBlit video;        // the surface-lock witness, owner thread only
std::atomic<std::uint64_t> video_foreign{0},video_early{0}; // in-range enters dropped: another thread / before the owner is admitted
std::uint32_t attempts_frame=0;            // owner thread only, reset at the frame boundary
std::uint64_t attempts_total=0,refused_total=0;
std::atomic<std::uint64_t> current_frame{0};
void* return_trampoline=nullptr;
bool lost_reported=false;
engine_patch::Site patches[sites::Count];
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
std::uint64_t qpc() noexcept {LARGE_INTEGER v{};return QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0;}
std::uint64_t to_us(std::uint64_t ticks) noexcept {return frequency?ticks*1000000ull/frequency:0;}
// The two-arm gate stub and its return trampoline, one arena block (298 bytes
// emitted, 300 after alignment). Entry: pushfd; push eax/ecx/edx; cld; sub
// esp,0x80; 8 movups saves; push esp (EnterFrame*); call enter; add esp,4;
// test eax,eax; jz refuse. PASS: 8 movups loads; add esp,0x80; pop edx/ecx/
// eax; popfd; jmp [next] (the claim tail). REFUSE: the same restore, then
// xor eax,eax; ret (cdecl: the caller pops the argument; the flags are those a
// real failed build leaves). Return trampoline: sub esp,4 (the slot); the same
// saves; push esp (ReturnFrame*); call ret_handler (returns the original
// return address); add esp,4; mov [esp+0x90],eax (into the slot); the same
// restore; ret (pops the slot, ESP is the caller's again). Layout offsets are
// checked as they are emitted; any deviation fails closed.
void* emit_gate(const void* enter,const void* ret_handler,void*** next_out,void** trampoline_out) {
    engine_patch::Emitter e(320);if(!e.ok())return nullptr;
    unsigned char* start=static_cast<unsigned char*>(e.here());
    const auto save=[&]{e.byte(0x9c);e.byte(0x50);e.byte(0x51);e.byte(0x52);e.byte(0xfc);e.byte(0x81);e.byte(0xec);e.dword(0x80);
        for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}};
    const auto restore=[&]{for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
        e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x5a);e.byte(0x59);e.byte(0x58);e.byte(0x9d);};
    if(reinterpret_cast<std::uintptr_t>(start)&3)return nullptr;
    save();
    e.byte(0x54);e.byte(0xe8);e.rel32(enter);e.byte(0x83);e.byte(0xc4);e.byte(4);
    e.byte(0x85);e.byte(0xc0);e.byte(0x74);e.byte(60); // jz refuse (start+124)
    if(e.here()!=start+64)return nullptr;
    restore();
    const auto next=reinterpret_cast<std::uintptr_t>(start)+120;
    e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));
    while(e.ok()&&reinterpret_cast<std::uintptr_t>(e.here())<next)e.byte(0xcc);
    if(e.here()!=start+120)return nullptr;
    e.dword(0);
    if(e.here()!=start+124)return nullptr;
    restore();e.byte(0x33);e.byte(0xc0);e.byte(0xc3);
    unsigned char* trampoline=static_cast<unsigned char*>(e.here());
    if(trampoline!=start+177)return nullptr;
    e.byte(0x83);e.byte(0xec);e.byte(4);
    save();
    e.byte(0x54);e.byte(0xe8);e.rel32(ret_handler);e.byte(0x83);e.byte(0xc4);e.byte(4);
    e.byte(0x89);e.byte(0x84);e.byte(0x24);e.dword(0x90);
    restore();e.byte(0xc3);
    if(e.here()!=start+298)return nullptr;
    if(!e.finish())return nullptr;
    *next_out=reinterpret_cast<void**>(next);
    if(trampoline_out)*trampoline_out=trampoline;
    return start;
}
void* emit(unsigned index,void*** next_out);
void reset_state() {
    gate.reset();cache.clear();pending={};ring={};limit={};enter_limit={};window.reset();video={};
    video_foreign.store(0,std::memory_order_relaxed);video_early.store(0,std::memory_order_relaxed);
    attempts_frame=0;attempts_total=refused_total=0;current_frame.store(0);lost_reported=false;
}
bool install_group(const engine_patch::SiteSpec* specs,const char*& status) {
    return_trampoline=nullptr;reset_state();
    return stamp::install_group(patches,specs,&emit,installed,status)&&return_trampoline;
}
const char* result_text(const detail::Entry& e,char (&buffer)[16]) {
    if(e.outcome==detail::unobserved)return "unobserved";
    if(!e.result)return "0";
    std::snprintf(buffer,sizeof buffer,"0x%08lx",static_cast<unsigned long>(e.result));
    return buffer;
}
const char* kind_text(const detail::Entry& e,char (&buffer)[16]) {
    if(e.caller==detail::selector||e.caller==detail::other)std::snprintf(buffer,sizeof buffer,"0x%lx",static_cast<unsigned long>(e.kind));
    else std::snprintf(buffer,sizeof buffer,"none");
    return buffer;
}
// The entry-side line: written synchronously from the gate, before the
// original allocator runs, straight to the session log's OS handle (no stdio
// buffer, no log mutex), so a build that never returns (run100/101: the first
// comm dialog hung inside Wine's video path) still names the cue that hung.
// Bypassing stdio means the line may precede buffered lines written earlier
// and, rarely, land inside a line stdio flushed in two pieces; grep for the
// prefix, not for line order. The CRT formatter is x87 code, so it runs under
// call_preserved's FNSAVE/FRSTOR envelope, indirectly, keeping the entry
// handler's audited graph x87-free. Reached only with the trace on and the
// entry limiter admitting; the trace-off path pays one predicate. A line
// that does not reach the file whole (no handle, truncated format, failed or
// short write) counts as suppressed so the window line accounts for it.
void write_enter_line(const detail::Entry& e) {
    const HANDLE handle=log_handle();
    bool written_whole=false;
    if(handle!=INVALID_HANDLE_VALUE&&handle)x3m::call_preserved([&]{
        char kind[16];char line[192];
        const int n=std::snprintf(line,sizeof line,"media_cue_enter frame=%llu qpc=%llu id=%lu kind=%s caller=%s flags=0x%lx attempt=%lu\n",
            e.frame,e.qpc,static_cast<unsigned long>(e.id),kind_text(e,kind),detail::caller_names[e.caller],static_cast<unsigned long>(e.flags),static_cast<unsigned long>(e.attempt));
        DWORD written=0;
        written_whole=n>0&&unsigned(n)<sizeof line&&WriteFile(handle,line,DWORD(n),&written,nullptr)&&written==DWORD(n);
    });
    if(!written_whole)++enter_limit.suppressed;
}
// The video blit witness line, the same direct handle write: the enter line
// goes down before the native LockRect/UnlockRect (either may be the call
// that never returns), the result line after it. Reached only for calls from
// the consumer's range on the owner thread with the trace on; GetDesc on the
// borrowed native surface is the only D3D call, made per written line (at
// most two per `video_blit_line_interval` blits plus the first unlock).
void write_video_line(const ownership::SurfaceLockEvent& e,const char* stage) {
    const HANDLE handle=log_handle();
    bool written_whole=false;
    if(handle!=INVALID_HANDLE_VALUE&&handle){
        D3DSURFACE_DESC desc{};
        if(!e.native||FAILED(e.native->GetDesc(&desc)))desc=D3DSURFACE_DESC{};
        char result[16];
        if(e.phase==ownership::SurfaceLockPhase::LockEnter||e.phase==ownership::SurfaceLockPhase::UnlockEnter)std::snprintf(result,sizeof result,"pending");
        else std::snprintf(result,sizeof result,"0x%08lx",static_cast<unsigned long>(e.result));
        char line[224];
        const int n=std::snprintf(line,sizeof line,"media_video_blit frame=%llu qpc=%llu texture=%p width=%u height=%u format=%u flags=0x%lx result=%s stage=%s blits=%llu unlocks=%llu\n",
            current_frame.load(std::memory_order_relaxed),qpc(),static_cast<void*>(e.surface),unsigned(desc.Width),unsigned(desc.Height),unsigned(desc.Format),
            static_cast<unsigned long>(e.flags),result,stage,video.locks_total,video.unlocks);
        DWORD written=0;
        written_whole=n>0&&unsigned(n)<sizeof line&&WriteFile(handle,line,DWORD(n),&written,nullptr)&&written==DWORD(n);
    }
    if(!written_whole)++video.suppressed;
}
// The observer the ownership shell calls twice per Surface::LockRect/UnlockRect
// once registered (trace on only): everything outside the consumer's return
// range costs the range compare; inside it, the counters and the cadenced
// lines. The shell's envelope restores x87/MXCSR/LastError around each call,
// so nothing is guarded here. An in-range call off the owner thread or before
// the owner is admitted is counted (video_foreign=/video_early=) and dropped,
// so a log with no blit line still tells "no blit" from "blit elsewhere".
// Never allocates, never takes the log mutex, never touches the surface
// beyond GetDesc.
void video_lock_observe(const ownership::SurfaceLockEvent& e) {
    if(!active.load(std::memory_order_relaxed))return;
    if(!detail::video_blit_caller(std::uint32_t(reinterpret_cast<std::uintptr_t>(e.return_address)),addresses))return;
    const DWORD owner=gate.owner.load(std::memory_order_relaxed);
    if(owner!=GetCurrentThreadId()){
        if(e.phase==ownership::SurfaceLockPhase::LockEnter||e.phase==ownership::SurfaceLockPhase::UnlockEnter)
            (owner?video_foreign:video_early).fetch_add(1,std::memory_order_relaxed);
        return;
    }
    switch(e.phase){
    case ownership::SurfaceLockPhase::LockEnter: if(video.lock_enter())write_video_line(e,"lock_enter"); break;
    case ownership::SurfaceLockPhase::LockResult: if(video.lock_result(FAILED(e.result)))write_video_line(e,"lock"); break;
    case ownership::SurfaceLockPhase::UnlockEnter: if(video.unlock_enter())write_video_line(e,"unlock_enter"); break;
    case ownership::SurfaceLockPhase::UnlockResult: if(video.unlock_result(FAILED(e.result)))write_video_line(e,"unlock"); break;
    }
}
void emit_entry(const detail::Entry& e) {
    char result[16];char kind[16];kind_text(e,kind);
    log("media_cue frame=%llu qpc=%llu id=%lu kind=%s caller=%s flags=0x%lx result=%s us=%llu attempts_frame=%lu cached=%u",
        e.frame,e.qpc,static_cast<unsigned long>(e.id),kind,detail::caller_names[e.caller],static_cast<unsigned long>(e.flags),
        result_text(e,result),e.outcome==detail::observed?to_us(e.duration_ticks):0ull,static_cast<unsigned long>(e.attempt),unsigned(e.outcome==detail::refused));
}
void emit_window() {
    detail::Summary s;
    if(!window.close(s))return;
    char ids[8*24+8];unsigned used=0;ids[0]=0;
    for(unsigned i=0;i<s.id_count;++i){
        const int n=std::snprintf(ids+used,sizeof ids-used,"%s%lu:%lu",i?",":"",static_cast<unsigned long>(s.ids[i].id),static_cast<unsigned long>(s.ids[i].count));
        if(n<=0||unsigned(n)>=sizeof ids-used)break;
        used+=unsigned(n);
    }
    log("media_cue_window qpc=%llu frame=%llu frames=%u attempts=%llu failures=%llu successes=%llu refused=%llu unobserved=%llu attempts_frame_p50=%llu attempts_frame_max=%lu ids=%s id_overflow=%llu cache_used=%u evictions=%llu depth_max=%u overflow=%llu stale=%llu mismatched=%llu lost=%llu suppressed=%llu enter_suppressed=%llu dropped=%llu early=%u foreign=%u video_blits=%llu video_unlocks=%llu video_failures=%llu video_suppressed=%llu video_reentries=%llu video_foreign=%llu video_early=%llu",
        qpc(),s.frame,s.frames,s.attempts,s.failures,s.successes,s.refused,s.unobserved,s.attempts_frame_p50,static_cast<unsigned long>(s.attempts_frame_max),
        s.id_count?ids:"none",s.id_overflow,cache.used,cache.evictions,pending.max_depth,pending.overflow,pending.stale,pending.mismatched,pending.lost,
        limit.suppressed,enter_limit.suppressed,ring.dropped,gate.early.exchange(0,std::memory_order_relaxed),gate.foreign.exchange(0,std::memory_order_relaxed),
        video.locks,video.unlocks,video.failures,video.suppressed,video.reentries,
        video_foreign.exchange(0,std::memory_order_relaxed),video_early.exchange(0,std::memory_order_relaxed));
    limit.suppressed=0;enter_limit.suppressed=0;ring.dropped=0;
    video.close();
    if(pending.lost&&!lost_reported){lost_reported=true;log("media_cue_lost count=%llu last_return=%08lx note=return_matched_no_pending_entry_fail_safe_used",pending.lost,static_cast<unsigned long>(pending.last_return));}
}
}
}

// The gate handler: called by the stub with flags, EAX/ECX/EDX and XMM0-7
// saved; returns 1 (PASS) or 0 (REFUSE). It must preserve everything else,
// execute no x87 opcode, never allocate and never take the log mutex; the one
// line it writes on a proceeded call with the trace on goes through
// write_enter_line's unbuffered handle write under the full envelope. A call
// before the owner is admitted or from another thread passes unobserved.
extern "C" __attribute__((force_align_arg_pointer)) unsigned __cdecl
x3m_media_cue_enter(x3m::media_cue::EnterFrame* f) {
    using namespace x3m::media_cue;
    if(!active.load(std::memory_order_relaxed))return 1;
    x3m::LightCallBoundary cpu; // MXCSR + LastError: QueryPerformanceCounter may set the last error
    if(!gate.owned(GetCurrentThreadId()))return 1;
    const detail::Caller caller=detail::classify(f->ret,f->slot_c,addresses);
    const bool scoped=detail::selector_scope(caller,f->slot_10,addresses);
    const std::uint64_t now=qpc();
    ++attempts_frame;++attempts_total;
    detail::Entry e;
    e.qpc=now;e.frame=current_frame.load(std::memory_order_relaxed);e.id=f->id;e.kind=f->slot_10;e.flags=f->eax;
    e.attempt=attempts_frame;e.caller=caller;e.scoped=scoped;
    const auto owned=owned_eligibility.load(std::memory_order_acquire);
    const bool bypass=f->id==2&&owned&&owned(2,f->eax?f->eax:8);
    if(cache_on&&scoped&&!bypass&&cache.refuses(f->id,now,retry_ticks)){
        ++refused_total;e.outcome=detail::refused;ring.push(e);
        return 0;
    }
    detail::Pending p;
    p.ret=f->ret;p.esp=std::uint32_t(reinterpret_cast<std::uintptr_t>(&f->ret));p.id=f->id;p.kind=f->slot_10;p.flags=f->eax;
    p.attempt=attempts_frame;p.qpc=now;p.frame=e.frame;p.caller=caller;p.scoped=scoped;
    if(pending.push(p))f->ret=std::uint32_t(reinterpret_cast<std::uintptr_t>(return_trampoline));
    else ring.push(e); // depth exhausted: proceed unobserved
    // Every proceeded call (observed or not) runs the allocator and can hang
    // in it; the REFUSE arm above never does, and its outcome line covers it.
    if(trace_on&&enter_limit.admit(now,frequency))write_enter_line(e);
    return 1;
}
// The return handler: EAX is the routine's result; returns the original
// return address for the slot. Populates the cache from the observed outcome.
extern "C" __attribute__((force_align_arg_pointer)) std::uint32_t __cdecl
x3m_media_cue_return(x3m::media_cue::ReturnFrame* f) {
    using namespace x3m::media_cue;
    x3m::LightCallBoundary cpu;
    const std::uint64_t now=qpc();
    detail::Pending p;
    // Unreachable by construction: every substituted return address is this
    // trampoline and every such call pushed an entry keyed by this slot. The
    // fail-safe returns the most recent substituted real return address
    // (counted in `lost`, reported once by the window line) so a hypothetical
    // miss does not `ret` to 0; it is a crash-avoidance measure, not a recovery.
    if(!pending.pop(std::uint32_t(reinterpret_cast<std::uintptr_t>(&f->slot)),p))return pending.last_return;
    if(f->id!=p.id)++pending.mismatched;
    if(f->eax)cache.success(p.id);
    else if(cache_on&&p.scoped)cache.fail(p.id,now);
    detail::Entry e;
    e.qpc=p.qpc;e.frame=p.frame;e.duration_ticks=now>=p.qpc?now-p.qpc:0;e.id=p.id;e.kind=p.kind;e.flags=p.flags;e.result=f->eax;
    e.attempt=p.attempt;e.caller=p.caller;e.outcome=detail::observed;e.scoped=p.scoped;
    ring.push(e);
    return p.ret;
}

namespace x3m::media_cue {
namespace {
void* emit(unsigned,void*** next_out) {
    return emit_gate(reinterpret_cast<const void*>(&x3m_media_cue_enter),reinterpret_cast<const void*>(&x3m_media_cue_return),next_out,&return_trampoline);
}
}
ownership::SurfaceLockObserver video_lock_observer() noexcept {
    return trace_on&&active.load(std::memory_order_acquire)?&video_lock_observe:nullptr;
}
bool initialize() {
    ErrorGuard error;
    if(initialized)return active.load(std::memory_order_acquire);
    initialized=true;wchar_t value[16]{};
    const bool trace_wanted=GetEnvironmentVariableW(L"X3M_MEDIA_CUE_TRACE",value,4)==1&&value[0]==L'1';
    const bool cache_wanted=GetEnvironmentVariableW(L"X3M_MEDIA_CUE_CACHE",value,4)==1&&value[0]==L'1';
    unsigned retry_s=default_retry_s;
    if(GetEnvironmentVariableW(L"X3M_MEDIA_CUE_RETRY_S",value,16)>0){const unsigned long n=wcstoul(value,nullptr,10);if(n>=1&&n<=max_retry_s)retry_s=unsigned(n);}
    if(!trace_wanted&&!cache_wanted)return false;
    trace_on=trace_wanted&&telemetry::enabled();
    cache_on=cache_wanted;
    const char* status=trace_wanted&&!trace_on&&!cache_on?"telemetry_off":"unset";
    if(trace_on||cache_on){
        LARGE_INTEGER f{};frequency=QueryPerformanceFrequency(&f)&&f.QuadPart>0?std::uint64_t(f.QuadPart):0;
        retry_ticks=frequency*retry_s;
        if(!frequency)status="clock_unavailable";
        else if(!object_trace::executable_verified())status="executable_unverified";
        else install_group(sites::kSites,status);
    }
    active.store(installed.load(std::memory_order_acquire),std::memory_order_release);
    log("media_cue_mode trace_requested=%u cache_requested=%u trace=%u cache=%u enabled=%u status=%s retry_s=%u cache_entries=%u pending_depth=%u ring=%u lines_per_second=%u window=%u owner=present_thread sector_change=interval_only qpc_frequency=%llu return_trampoline=%08lx",
        unsigned(trace_wanted),unsigned(cache_wanted),unsigned(trace_on&&active.load()),unsigned(cache_on&&active.load()),unsigned(active.load()),status,retry_s,
        detail::cache_entries,detail::pending_depth,detail::ring_entries,detail::lines_per_second,detail::window_frames,frequency,reinterpret_cast<unsigned long>(return_trampoline));
    for(unsigned i=0;i<sites::Count;++i)log("media_cue_site index=%u address=%08lx length=%u rel32=%u patched=%u status=%s",i,
        static_cast<unsigned long>(sites::kSites[i].address),sites::kSites[i].length,sites::kSites[i].rel32_offset,unsigned(patches[i].patched_in),patches[i].status);
    return active.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame) noexcept {
    ErrorGuard error;
    if(!gate.admit(GetCurrentThreadId()))return;
    current_frame.store(frame,std::memory_order_relaxed);
    window.frame(frame,attempts_frame);attempts_frame=0;
    Entry e;
    while(ring.pop(e)){
        window.entry(e);
        if(trace_on&&limit.admit(e.qpc,frequency))emit_entry(e);
    }
    if(window.full())emit_window();
}
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_install(const engine_patch::SiteSpec* specs,const detail::Addresses& fixture_addresses,bool trace,bool cache_enabled,std::uint64_t ticks,const char** status) {
    const char* text="unset";
    LARGE_INTEGER f{};frequency=QueryPerformanceFrequency(&f)&&f.QuadPart>0?std::uint64_t(f.QuadPart):0;
    addresses=fixture_addresses;trace_on=trace;cache_on=cache_enabled;retry_ticks=ticks;
    const bool okay=frequency&&install_group(specs,text);
    if(!frequency)text="clock_unavailable";
    active.store(installed.load(std::memory_order_acquire));
    reset_state();
    if(status)*status=text;
    return okay;
}
bool fixture_uninstall() {
    active.store(false);installed.store(false,std::memory_order_release);
    return stamp::uninstall_group(patches);
}
void* fixture_emit(void*** next){return emit(0,next);}
const detail::NegativeCache* fixture_cache(){return &cache;}
const detail::PendingStack* fixture_pending(){return &pending;}
const detail::Gate* fixture_gate(){return &gate;}
bool fixture_pop_trace(detail::Entry* out) {
    if(!out||gate.owner.load()!=GetCurrentThreadId())return false;
    detail::Entry e;if(!ring.pop(e))return false;
    window.entry(e);*out=e;return true;
}
std::uint32_t fixture_attempts_frame(){return attempts_frame;}
std::uint64_t fixture_refused(){return refused_total;}
const char* fixture_site_status(){return patches[0].status;}
void fixture_drop_pending(){pending.depth=0;} // models a return whose entry vanished: the next return is `lost`
const detail::RateLimit* fixture_enter_limit(){return &enter_limit;}
void fixture_reset_enter_limit(){enter_limit={};}
const detail::VideoBlit* fixture_video(){return &video;}
std::uint64_t fixture_video_dropped(bool foreign){return (foreign?video_foreign:video_early).load(std::memory_order_relaxed);}
void fixture_reset_video(){video={};video_foreign.store(0);video_early.store(0);}
#endif
}
