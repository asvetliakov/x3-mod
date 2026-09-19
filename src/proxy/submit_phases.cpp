#include "submit_phases.h"
#include "submit_phase_sites.h"
#include "frame_phases.h"
#include "lean_stub.h"
#include "stamp_install.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "telemetry.h"
#include "capture.h"
#include <atomic>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
static_assert(x3m::submit_phases::detail::site_count==x3m::submit_phases::sites::Count,"role table and site table are one list");
static_assert(x3m::submit_phases::detail::sort_enter_site==x3m::submit_phases::sites::SortEnter
    &&x3m::submit_phases::detail::walk_begin_site==x3m::submit_phases::sites::WalkBegin
    &&x3m::submit_phases::detail::walk_miss_site==x3m::submit_phases::sites::WalkMiss
    &&x3m::submit_phases::detail::walk_join_site==x3m::submit_phases::sites::WalkJoin
    &&x3m::submit_phases::detail::end_begin_site==x3m::submit_phases::sites::EndBegin,"special sites keep their table positions");
namespace x3m::submit_phases {
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
engine_patch::Site patches[sites::Count];
// Engine layout read by the two counters (view-submit-hot-path.md 5.1, 5.4):
// the view root global the sort loads into EDI at 0x0047e62a, its queue head
// (+0x40) and sentinel (+0x44), entries linked through their first word; the
// view's node-cache list head (+0x2a0), records linked through their first
// word and ended by a record whose link is null.
const std::uint32_t* view_root_global=reinterpret_cast<const std::uint32_t*>(0x00608518);
constexpr std::uint32_t queue_head_offset=0x40,queue_sentinel_offset=0x44,cache_head_offset=0x2a0;
constexpr std::uint32_t chase_cap=1u<<16; // a corrupt or cyclic list costs a bounded walk, never a hang
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
void* emit(unsigned index,void*** next_out);
bool install_group(const engine_patch::SiteSpec* specs,const char*& status) {
    return stamp::install_group(patches,specs,&emit,installed,status);
}
inline std::uint32_t word(std::uint32_t address) noexcept { return *reinterpret_cast<const std::uint32_t*>(address); }
// Entries the sort is about to order: the engine dereferences the same root
// and head unconditionally in its next four instructions.
std::uint32_t queue_length() noexcept {
    const std::uint32_t root=*view_root_global;if(!root)return 0;
    const std::uint32_t sentinel=root+queue_sentinel_offset;
    std::uint32_t count=0;
    for(std::uint32_t entry=word(root+queue_head_offset);entry&&entry!=sentinel&&count<chase_cap;entry=word(entry))++count;
    return count;
}
// Iterations the engine's walk at 0x0047e26e-0x0047e283 just made: records
// compared up to and including `hit`, or every linked record on a miss (hit=0).
std::uint32_t walk_length(std::uint32_t view,std::uint32_t hit) noexcept {
    if(!view)return 0;
    std::uint32_t count=0;
    for(std::uint32_t record=word(view+cache_head_offset);record&&count<chase_cap;){
        const std::uint32_t next=word(record);if(!next)break;
        ++count;if(record==hit)break;
        record=next;
    }
    return count;
}
void emit_window() {
    detail::Summary s;
    if(!window.close(s))return;
    // One clock read per window; see frame_phases::emit_window for qpc=.
    LARGE_INTEGER v{};
    const std::uint64_t emitted=QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0;
    using namespace detail;
    log("submit_phases qpc=%llu frame=%llu frames=%u stamps_p50=%llu stamps_p95=%llu self_p50_us=%llu dispatch_cost_ns=%llu "
        "sort_calls_p50=%llu sort_p50_us=%llu sort_p95_us=%llu sort_nodes_p50=%llu sort_nodes_max=%llu "
        "walk_calls_p50=%llu walk_p50_us=%llu walk_p95_us=%llu walk_misses_p50=%llu walk_iterations_p50=%llu walk_iterations_p95=%llu walk_sample_period=%u "
        "technique_calls_p50=%llu technique_p50_us=%llu technique_p95_us=%llu end_calls_p50=%llu end_p50_us=%llu end_p95_us=%llu "
        "block_calls_p50=%llu block_p50_us=%llu block_p95_us=%llu block_net_p50_us=%llu "
        "inverse_world_calls_p50=%llu inverse_world_p50_us=%llu inverse_world_p95_us=%llu inverse_view_calls_p50=%llu inverse_view_p50_us=%llu inverse_view_p95_us=%llu "
        "material_calls_p50=%llu material_p50_us=%llu material_p95_us=%llu material_net_p50_us=%llu world_calls_p50=%llu world_p50_us=%llu world_p95_us=%llu "
        "block_skipped=%llu reopened=%llu idle=%llu clock_errors=%llu clock_failures=%llu unmatched=%llu dropped=%llu early=%u foreign=%u",
        emitted,s.frame,s.frames,s.stamps_p50,s.stamps_p95,s.self_p50,dispatch_cost_ns,
        s.calls_p50[Sort],s.interval_p50[Sort],s.interval_p95[Sort],s.sort_nodes_p50,s.sort_nodes_max,
        s.calls_p50[Walk],s.interval_p50[Walk],s.interval_p95[Walk],s.walk_misses_p50,s.walk_iterations_p50,s.walk_iterations_p95,walk_sample_period,
        s.calls_p50[Technique],s.interval_p50[Technique],s.interval_p95[Technique],s.calls_p50[End],s.interval_p50[End],s.interval_p95[End],
        s.calls_p50[Block],s.interval_p50[Block],s.interval_p95[Block],s.block_net_p50,
        s.calls_p50[InverseWorld],s.interval_p50[InverseWorld],s.interval_p95[InverseWorld],s.calls_p50[InverseView],s.interval_p50[InverseView],s.interval_p95[InverseView],
        s.calls_p50[Material],s.interval_p50[Material],s.interval_p95[Material],s.material_net_p50,s.calls_p50[World],s.interval_p50[World],s.interval_p95[World],
        accumulator.block_skipped,accumulator.reopened,accumulator.idle,accumulator.clock_errors,accumulator.clock_failures,accumulator.unmatched,dropped,
        gate.early.exchange(0,std::memory_order_relaxed),gate.foreign.exchange(0,std::memory_order_relaxed));
    accumulator.block_skipped=accumulator.reopened=accumulator.idle=0;
    accumulator.clock_errors=accumulator.clock_failures=accumulator.unmatched=0;dropped=0;
}
}
}

// The per-dispatch handler: called by the context stub with flags, all eight
// general registers and XMM0-7 already saved; it executes no x87 opcode, never
// logs and never allocates. `saved` is the stub's pushad frame (lean_stub.h
// SavedRegister), read-only. The owner check is one relaxed load.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_submit_phase_enter(unsigned index,const std::uint32_t* saved) {
    using namespace x3m::submit_phases;
    if(!active.load(std::memory_order_relaxed))return;
    x3m::LightCallBoundary cpu; // MXCSR + LastError: QueryPerformanceCounter may set the last error
    if(!gate.owned(GetCurrentThreadId()))return;
    // The queue count runs before the sort's clock opens; the walk count after
    // the walk's clock closed: neither is inside the interval it describes.
    if(index==sites::SortEnter)accumulator.sorted(queue_length());
    const bool count_walk=(index==sites::WalkMiss||index==sites::WalkJoin)&&accumulator.open_clock[detail::Walk]&&accumulator.walk_sampled();
    LARGE_INTEGER v{};
    const std::uint64_t now=QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0;
    accumulator.stamp(index,now);
    if(count_walk)accumulator.walked(walk_length(saved[x3m::lean_stub::SavedEdi],index==sites::WalkJoin?saved[x3m::lean_stub::SavedEsi]:0));
}

namespace x3m::submit_phases {
namespace {
void* emit(unsigned index,void*** next_out) {
    return lean_stub::emit_context(reinterpret_cast<const void*>(&x3m_submit_phase_enter),index,next_out);
}
}
bool initialize() {
    ErrorGuard error;
    if(initialized)return active.load(std::memory_order_acquire);
    initialized=true;wchar_t value[4]{};
    const bool wanted=GetEnvironmentVariableW(L"X3M_SUBMIT_PHASES",value,4)==1&&value[0]==L'1';
    if(!wanted)return false;
    const char* status="telemetry_off";
    if(telemetry::enabled()){
        frequency=telemetry::frequency();
        if(!frequency)status="clock_unavailable";
        else if(!frame_phases::active.load(std::memory_order_acquire))status="frame_phases_off"; // the frame boundary and the owner thread come from the frame group
        else if(!object_trace::executable_verified())status="executable_unverified";
        else install_group(sites::kSites,status);
    }
    active.store(installed.load(std::memory_order_acquire),std::memory_order_release);
    log("submit_phase_mode requested=1 enabled=%u status=%s sites=%u window=%u owner=present_thread dispatch_cost_ns=%llu walk_sample_period=%u qpc_frequency=%llu",
        unsigned(active.load()),status,sites::Count,detail::window_frames,detail::dispatch_cost_ns,detail::walk_sample_period,frequency);
    for(unsigned i=0;i<sites::Count;++i)log("submit_phase_site index=%u address=%08lx length=%u patched=%u status=%s",i,
        static_cast<unsigned long>(sites::kSites[i].address),sites::kSites[i].length,unsigned(patches[i].patched_in),patches[i].status);
    return active.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame,bool sampled) noexcept {
    ErrorGuard error;
    if(!gate.admit(GetCurrentThreadId()))return;
    if(!sampled){accumulator.discard();++dropped;return;}
    accumulator.take(frame,frequency,last_sample);
    window.add(last_sample);
    if(window.full())emit_window();
}
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_install(const engine_patch::SiteSpec* specs,const char** status,const std::uint32_t* root_global) {
    const char* text="unset";
    LARGE_INTEGER f{};frequency=QueryPerformanceFrequency(&f)&&f.QuadPart>0?std::uint64_t(f.QuadPart):0;
    view_root_global=root_global;
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
