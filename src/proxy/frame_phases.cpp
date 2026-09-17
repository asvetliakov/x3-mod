#include "frame_phases.h"
#include "frame_phase_sites.h"
#include "game_phases.h"
#include "game_phase_sites.h"
#include "pass_phases.h"
#include "residual_phases.h"
#include "loop_phases.h"
#include "object_trace.h"
#include "telemetry.h"
#include "capture.h"
#include <atomic>
#include <cstdio>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
namespace x3m::frame_phases {
std::atomic<bool> active{false};
namespace {
std::atomic<bool> installed{false};
std::atomic<DWORD> owner_thread{0};
std::atomic<std::uint32_t> foreign_hits{0},early_hits{0};
bool initialized=false;
std::uint64_t frequency=0;
detail::Tracker tracker;
detail::Window window;
detail::Sample last_sample;
engine_patch::Site patches[sites::Count];
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
std::uint64_t qpc() noexcept {LARGE_INTEGER v{};return QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0;}
// The Present path admits the main-loop thread (the same thread the game-phase
// group proves for its LoopSetup/Present pair); a stamp before the first
// Present or from any other thread is counted and ignored.
bool owner(bool admit) noexcept {
    const DWORD thread=GetCurrentThreadId();DWORD expected=0;
    if(admit)owner_thread.compare_exchange_strong(expected,thread,std::memory_order_acq_rel);
    const DWORD current=owner_thread.load(std::memory_order_acquire);
    if(current==thread)return true;
    (current?foreign_hits:early_hits).fetch_add(1,std::memory_order_relaxed);
    return false;
}
// Same transaction shape as game_phases::install_group: preflight every span,
// claim in order, roll back every patched site on the first failure, and only
// then activate. Refused outside the install window.
bool install_group(const engine_patch::SiteSpec* specs,const char*& status) {
    installed.store(false,std::memory_order_release);
    if(!engine_patch::install_window_open()){status="install_window_closed";return false;}
    for(unsigned i=0;i<sites::Count;++i)
        if(!engine_patch::verify_bytes(specs[i].address,specs[i].expected,specs[i].length)){status="preflight_bytes";return false;}
    for(unsigned i=0;i<sites::Count;++i){
        if(engine_patch::claim(patches[i],specs[i])){
            void** next=nullptr;void* stub=game_phases::emit_stub(game_phases::sites::Count+i,&next);
            if(stub&&next&&engine_patch::store_pointer(next,*patches[i].entry)&&engine_patch::push_front(patches[i],stub))continue;
            status="stub_chain_failed";
        }else status=patches[i].status; // the claim's own reason (bytes_mismatch, late_claim, arena_full, ...)
        bool restored=true;
        for(unsigned j=sites::Count;j-->0;)if(patches[j].patched_in&&!engine_patch::restore(patches[j]))restored=false;
        if(!restored)status="rollback_failed_inert";
        return false;
    }
    status="ok";installed.store(true,std::memory_order_release);return true;
}
// Field text of one line: 72 bytes per phase pair covers realistic microsecond
// values (the widest pair name plus two 20-digit values would need 82);
// the reserved tail carries `truncated=1` if a field ever does not fit, so a
// dropped field is never silent.
constexpr unsigned phase_text_tail=16,phase_text_capacity=(detail::phase_count+2)*72+phase_text_tail;
struct PhaseText {
    char text[phase_text_capacity]{};unsigned used=0;bool truncated=false;
    template<class... A> void add(const char* format,A... a) noexcept {
        const unsigned room=phase_text_capacity-phase_text_tail-used;
        const int n=std::snprintf(text+used,room,format,a...);
        if(n>0&&unsigned(n)<room)used+=unsigned(n);
        else {text[used]=0;truncated=true;}
    }
    const char* finish() noexcept {
        if(truncated)std::snprintf(text+used,phase_text_capacity-used," truncated=1");
        return text;
    }
};
void emit_window() {
    detail::Summary s;
    if(!window.close(s))return;
    // One clock read per window: qpc= on the window line places it on the
    // session's clock_anchor line, so a wall-clock stamp from another component
    // can be aligned with these frames (docs/verification/sampling-profiler.md,
    // "Audio correlation"). The frame_phases_slow lines below carry no qpc=:
    // they name frames inside the window this line closes.
    const std::uint64_t emitted=qpc();
    PhaseText phases;
    for(unsigned i=0;i<detail::phase_count;++i)phases.add(" %s_p50_us=%llu %s_p95_us=%llu",detail::phase_names[i],s.phase_p50[i],detail::phase_names[i],s.phase_p95[i]);
    phases.add(" view_setup_p50_us=%llu view_setup_p95_us=%llu",s.view_setup_p50,s.view_setup_p95);
    phases.add(" view_submit_p50_us=%llu view_submit_p95_us=%llu",s.view_submit_p50,s.view_submit_p95);
    log("frame_phases qpc=%llu frame=%llu frames=%u incomplete=%u dt_p50_us=%llu dt_p95_us=%llu%s views_p50=%llu order_errors=%llu clock_errors=%llu unmatched=%llu dropped=%llu early=%u foreign=%u",
        emitted,s.frame,s.frames,s.incomplete,s.dt_p50,s.dt_p95,phases.finish(),s.views_p50,tracker.order_errors,tracker.clock_errors,tracker.unmatched,tracker.dropped,
        early_hits.exchange(0,std::memory_order_relaxed),foreign_hits.exchange(0,std::memory_order_relaxed));
    tracker.order_errors=tracker.clock_errors=tracker.unmatched=tracker.dropped=0;
    for(unsigned i=0;i<s.slow_frames_count;++i){
        const auto& f=s.slow_frames[i];PhaseText slow;
        for(unsigned p=0;p<detail::phase_count;++p)slow.add(" %s_us=%llu",detail::phase_names[p],f.phase_us[p]);
        log("frame_phases_slow frame=%llu dt_us=%llu%s view_setup_us=%llu view_submit_us=%llu views=%u complete=%u",
            f.frame,f.dt_us,slow.finish(),f.view_setup_us,f.view_submit_us,f.views,unsigned(f.complete));
    }
}
}
bool initialize() {
    ErrorGuard error;
    if(initialized)return active.load(std::memory_order_acquire);
    initialized=true;wchar_t value[4]{};
    const bool wanted=GetEnvironmentVariableW(L"X3M_FRAME_PHASES",value,4)==1&&value[0]==L'1';
    if(!wanted)return false;
    const char* status="telemetry_off";
    if(telemetry::enabled()){
        frequency=telemetry::frequency();
        if(!frequency)status="clock_unavailable";
        else if(!object_trace::executable_verified())status="executable_unverified";
        else install_group(sites::kSites,status);
    }
    active.store(installed.load(std::memory_order_acquire),std::memory_order_release);
    log("frame_phase_mode requested=1 enabled=%u status=%s sites=%u window=%u slow_slots=%u owner=present_thread qpc_frequency=%llu",
        unsigned(active.load()),status,sites::Count,detail::window_frames,detail::slow_slots,frequency);
    for(unsigned i=0;i<sites::Count;++i)log("frame_phase_site index=%u address=%08lx length=%u rel32=%u patched=%u status=%s",i,
        static_cast<unsigned long>(sites::kSites[i].address),sites::kSites[i].length,sites::kSites[i].rel32_offset,unsigned(patches[i].patched_in),patches[i].status);
    return active.load(std::memory_order_acquire);
}
void stamp(unsigned index) noexcept {
    if(!active.load(std::memory_order_relaxed)||!owner(false))return;
    tracker.site(index,qpc());
}
const detail::Tracker* shared_tracker() noexcept {return &tracker;}
namespace detail {
void present_begin_impl() noexcept {
    ErrorGuard error;
    if(!owner(true))return;
    tracker.present_begin(qpc());
}
void present_end_impl() noexcept {
    ErrorGuard error;
    if(!owner(false))return;
    tracker.present_end(qpc());
}
void frame_impl(std::uint64_t frame) noexcept {
    ErrorGuard error;
    if(!owner(false))return;
    const bool taken=tracker.take(frame,frequency,last_sample);
    residual_phases::frame(frame,taken,taken?last_sample.phase_us[detail::views_phase]:0,taken?last_sample.view_setup_us:0,taken?last_sample.view_submit_us:0,taken?last_sample.views:0); // X3M_RESIDUAL_PHASES only: same guard, ahead of the pass group so its accumulator is still open
    pass_phases::frame(frame,taken,taken?last_sample.view_submit_us:0); // X3M_PASS_PHASES only: closes the frame's pass accumulators under this guard
    loop_phases::frame(frame,taken,taken?last_sample.dt_us:0,taken?last_sample.phase_us[detail::pre_render]:0); // X3M_LOOP_PHASES only: same guard, joins dt and pre_render
    if(!taken)return;
    window.add(last_sample);
    if(window.full())emit_window();
}
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_install(const engine_patch::SiteSpec* specs,const char** status) {
    const char* text="unset";
    LARGE_INTEGER f{};frequency=QueryPerformanceFrequency(&f)&&f.QuadPart>0?std::uint64_t(f.QuadPart):0;
    const bool okay=frequency&&install_group(specs,text);
    if(!frequency)text="clock_unavailable";
    active.store(installed.load(std::memory_order_acquire));
    owner_thread.store(0);tracker={};window.reset();last_sample={};
    if(status)*status=text;
    return okay;
}
bool fixture_uninstall() {
    active.store(false);installed.store(false,std::memory_order_release);
    bool restored=true;
    for(unsigned i=sites::Count;i-->0;){
        if(patches[i].patched_in&&!engine_patch::restore(patches[i]))restored=false;
        patches[i]=engine_patch::Site{};
    }
    return restored;
}
bool fixture_last_sample(detail::Sample* out) {
    if(!out||owner_thread.load()!=GetCurrentThreadId())return false;
    *out=last_sample;return true;
}
const detail::Tracker* fixture_tracker(){return &tracker;}
#endif
}
