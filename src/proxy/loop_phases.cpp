#include "loop_phases.h"
#include "loop_phase_sites.h"
#include "frame_phases.h"
#include "game_phases.h"
#include "lean_stub.h"
#include "stamp_install.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "telemetry.h"
#include "log_tiers.h"
#include "capture.h"
#include <atomic>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
namespace x3m::loop_phases {
std::atomic<bool> active{false};
namespace {
std::atomic<bool> installed{false};
bool initialized=false;
std::uint64_t frequency=0;
std::uint64_t dropped=0; // frames closed without a frame-phase sample (owner thread only)
detail::Gate gate;
detail::Accumulator accumulator;
detail::Window window;
detail::Sample last_sample;
detail::Summary summary; // static: carries up to 64 slow-frame witnesses, filled once per window
engine_patch::Site patches[sites::Count];
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
void* emit(unsigned index,void*** next_out);
// The shared transaction (stamp_install.h): preflight every span, claim in
// order, roll back every patched site on the first failure, activate last.
bool install_group(const engine_patch::SiteSpec* specs,const char*& status) {
    return stamp::install_group(patches,specs,&emit,installed,status);
}
void emit_window() {
    detail::Summary& s=summary;
    if(!window.close(s))return;
    // One clock read per window, carried by the window line and by every
    // slow-frame line closed with it; see frame_phases::emit_window for qpc=.
    LARGE_INTEGER v{};
    const std::uint64_t emitted=QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0;
    log("loop_phases qpc=%llu frame=%llu frames=%u sectors_p50=%llu containers_p50=%llu collide_p50_us=%llu collide_p95_us=%llu simulate_p50_us=%llu simulate_p95_us=%llu post_p50_us=%llu post_p95_us=%llu passb_p50_us=%llu passb_p95_us=%llu sum_p50_us=%llu input_p50_us=%llu self_p50_us=%llu dispatch_cost_ns=%llu max_interval_us=%llu max_interval_owner=%s slow=%u orphans=%llu clock_errors=%llu clock_failures=%llu unmatched=%llu dropped=%llu early=%u foreign=%u",
        emitted,s.frame,s.frames,s.sectors_p50,s.containers_p50,s.interval_p50[0],s.interval_p95[0],s.interval_p50[1],s.interval_p95[1],s.interval_p50[2],s.interval_p95[2],s.interval_p50[3],s.interval_p95[3],
        s.sum_p50,s.input_p50,s.self_p50,detail::dispatch_cost_ns,s.max_interval_us,detail::interval_names[s.max_owner],s.slow,
        accumulator.orphans,accumulator.clock_errors,accumulator.clock_failures,accumulator.unmatched,dropped,
        gate.early.exchange(0,std::memory_order_relaxed),gate.foreign.exchange(0,std::memory_order_relaxed));
    accumulator.orphans=accumulator.clock_errors=accumulator.clock_failures=accumulator.unmatched=0;dropped=0;
    for(unsigned i=0;i<s.slow_count;++i){
        const auto& f=s.slow_frames[i];
        log("loop_phases_slow qpc=%llu frame=%llu dt_us=%llu sectors=%u containers=%u collide_us=%llu simulate_us=%llu post_us=%llu passb_us=%llu sum_us=%llu input_us=%llu max_interval_us=%llu max_interval_owner=%s",
            emitted,f.frame,f.dt_us,f.sectors,f.containers,f.interval_us[0],f.interval_us[1],f.interval_us[2],f.interval_us[3],f.sum_us,f.input_us,f.max_interval_us,detail::interval_names[f.max_owner]);
    }
}
}
}

// The per-dispatch handler: called by the lean stub with flags, EAX/ECX/EDX
// and XMM0-7 already saved; it must preserve everything else, execute no x87
// opcode, never log and never allocate. The owner check is one relaxed load.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_loop_phase_enter(unsigned index) {
    using namespace x3m::loop_phases;
    if(!active.load(std::memory_order_relaxed))return;
    x3m::LightCallBoundary cpu; // MXCSR + LastError: QueryPerformanceCounter may set the last error
    if(!gate.owned(GetCurrentThreadId()))return;
    LARGE_INTEGER v{};
    accumulator.stamp(index,QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0);
}

namespace x3m::loop_phases {
namespace {
// The shared lean stub (lean_stub.cpp) entering x3m_loop_phase_enter.
void* emit(unsigned index,void*** next_out) {
    return lean_stub::emit(reinterpret_cast<const void*>(&x3m_loop_phase_enter),index,next_out);
}
}
bool initialize() {
    ErrorGuard error;
    if(initialized)return active.load(std::memory_order_acquire);
    initialized=true;
    const bool wanted=log_tier::draw_trace_flag(L"X3M_LOOP_PHASES"); // X3M_LOOP_PHASES=1 or X3M_DRAW_TRACE=1 (log_tiers.h)
    if(!wanted)return false;
    const char* status="telemetry_off";
    if(telemetry::enabled()){
        frequency=telemetry::frequency();
        if(!frequency)status="clock_unavailable";
        else if(!frame_phases::active.load(std::memory_order_acquire))status="frame_phases_off"; // the frame boundary, dt and pre_render come from the frame group
        else if(!object_trace::executable_verified())status="executable_unverified";
        else install_group(sites::kSites,status);
    }
    active.store(installed.load(std::memory_order_acquire),std::memory_order_release);
    log("loop_phase_mode requested=1 enabled=%u status=%s sites=%u window=%u slow_threshold_us=%llu slow_limit=%u owner=present_thread dispatch_cost_ns=%llu qpc_frequency=%llu",
        unsigned(active.load()),status,sites::Count,detail::window_frames,detail::slow_threshold_us,detail::slow_limit,detail::dispatch_cost_ns,frequency);
    for(unsigned i=0;i<sites::Count;++i)log("loop_phase_site index=%u address=%08lx length=%u rel32=%u patched=%u status=%s",i,
        static_cast<unsigned long>(sites::kSites[i].address),sites::kSites[i].length,sites::kSites[i].rel32_offset,unsigned(patches[i].patched_in),patches[i].status);
    return active.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame,bool sampled,std::uint64_t dt_us,std::uint64_t pre_render_us) noexcept {
    ErrorGuard error;
    if(!gate.admit(GetCurrentThreadId()))return;
    if(!sampled){accumulator.discard();++dropped;return;}
    std::uint64_t input_us=pre_render_us;
    game_phases::last_input_us(&input_us); // X3M_GAME_PHASES only: the last completed input phase replaces pre_render
    accumulator.take(frame,frequency,dt_us,input_us,last_sample);
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
    gate.reset();accumulator={};window.reset();last_sample={};dropped=0;
    if(status)*status=text;
    return okay;
}
bool fixture_uninstall() {
    active.store(false);installed.store(false,std::memory_order_release);
    return stamp::uninstall_group(patches);
}
void* fixture_emit(unsigned index,void*** next){return emit(index,next);}
bool fixture_last_sample(detail::Sample* out) {
    if(!out||gate.owner.load()!=GetCurrentThreadId())return false;
    *out=last_sample;return true;
}
const detail::Accumulator* fixture_accumulator(){return &accumulator;}
const detail::Gate* fixture_gate(){return &gate;}
std::uint64_t fixture_dropped(){return dropped;}
#endif
}
