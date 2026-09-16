#include "pass_phases.h"
#include "pass_phase_sites.h"
#include "frame_phases.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "telemetry.h"
#include "capture.h"
#include <atomic>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
namespace x3m::pass_phases {
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
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
void* emit(unsigned index,void*** next_out);
// Same transaction shape as frame_phases::install_group: preflight every span,
// claim in order, roll back every patched site on the first failure, and only
// then activate. Refused outside the install window.
bool install_group(const engine_patch::SiteSpec* specs,const char*& status) {
    installed.store(false,std::memory_order_release);
    if(!engine_patch::install_window_open()){status="install_window_closed";return false;}
    for(unsigned i=0;i<sites::Count;++i)
        if(!engine_patch::verify_bytes(specs[i].address,specs[i].expected,specs[i].length)){status="preflight_bytes";return false;}
    for(unsigned i=0;i<sites::Count;++i){
        if(engine_patch::claim(patches[i],specs[i])){
            void** next=nullptr;void* stub=emit(i,&next);
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
void emit_window() {
    detail::Summary s;
    if(!window.close(s))return;
    log("pass_phases frame=%llu frames=%u passes_p50=%llu apply_p50_us=%llu apply_p95_us=%llu draw_p50_us=%llu draw_p95_us=%llu end_p50_us=%llu end_p95_us=%llu sum_p50_us=%llu view_submit_p50_us=%llu self_p50_us=%llu dispatch_cost_ns=%llu orphans=%llu clock_errors=%llu clock_failures=%llu unmatched=%llu dropped=%llu early=%u foreign=%u",
        s.frame,s.frames,s.passes_p50,s.interval_p50[0],s.interval_p95[0],s.interval_p50[1],s.interval_p95[1],s.interval_p50[2],s.interval_p95[2],
        s.sum_p50,s.view_submit_p50,s.self_p50,detail::dispatch_cost_ns,accumulator.orphans,accumulator.clock_errors,accumulator.clock_failures,accumulator.unmatched,dropped,
        gate.early.exchange(0,std::memory_order_relaxed),gate.foreign.exchange(0,std::memory_order_relaxed));
    accumulator.orphans=accumulator.clock_errors=accumulator.clock_failures=accumulator.unmatched=0;dropped=0;
}
}
}

// The per-dispatch handler: called by the lean stub with flags, EAX/ECX/EDX
// and XMM0-7 already saved; it must preserve everything else, execute no x87
// opcode, never log and never allocate. The owner check is one relaxed load.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_pass_phase_enter(unsigned index) {
    using namespace x3m::pass_phases;
    if(!active.load(std::memory_order_relaxed))return;
    x3m::LightCallBoundary cpu; // MXCSR + LastError: QueryPerformanceCounter may set the last error
    if(!gate.owned(GetCurrentThreadId()))return;
    LARGE_INTEGER v{};
    accumulator.stamp(index,QueryPerformanceCounter(&v)&&v.QuadPart>0?std::uint64_t(v.QuadPart):0);
}

namespace x3m::pass_phases {
namespace {
// Lean stub, 124 bytes: pushfd; push eax/ecx/edx; cld; sub esp,0x80; 8 movups
// saves; push index; call x3m_pass_phase_enter; add esp,4; 8 movups loads;
// add esp,0x80; pop edx/ecx/eax; popfd; jmp [next]. The handler is cdecl and
// keeps EBX/ESI/EDI/EBP itself; the displaced instructions then run in the
// claim tail at the game's exact ESP with every register and the flags
// restored. No x87 save: the handler executes no x87 opcode (cpu_state.h,
// LightCallBoundary precondition; check_no_x87.py).
void* emit(unsigned index,void*** next_out) {
    engine_patch::Emitter e(160);if(!e.ok())return nullptr;void* start=e.here();
    e.byte(0x9c);e.byte(0x50);e.byte(0x51);e.byte(0x52);e.byte(0xfc);
    e.byte(0x81);e.byte(0xec);e.dword(0x80);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x68);e.dword(index);e.byte(0xe8);e.rel32(reinterpret_cast<const void*>(&x3m_pass_phase_enter));
    e.byte(0x83);e.byte(0xc4);e.byte(4);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x5a);e.byte(0x59);e.byte(0x58);e.byte(0x9d);
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
    const bool wanted=GetEnvironmentVariableW(L"X3M_PASS_PHASES",value,4)==1&&value[0]==L'1';
    if(!wanted)return false;
    const char* status="telemetry_off";
    if(telemetry::enabled()){
        frequency=telemetry::frequency();
        if(!frequency)status="clock_unavailable";
        else if(!frame_phases::active.load(std::memory_order_acquire))status="frame_phases_off"; // the frame boundary and view_submit come from the frame group
        else if(!object_trace::executable_verified())status="executable_unverified";
        else install_group(sites::kSites,status);
    }
    active.store(installed.load(std::memory_order_acquire),std::memory_order_release);
    log("pass_phase_mode requested=1 enabled=%u status=%s sites=%u window=%u owner=present_thread dispatch_cost_ns=%llu qpc_frequency=%llu",
        unsigned(active.load()),status,sites::Count,detail::window_frames,detail::dispatch_cost_ns,frequency);
    for(unsigned i=0;i<sites::Count;++i)log("pass_phase_site index=%u address=%08lx length=%u patched=%u status=%s",i,
        static_cast<unsigned long>(sites::kSites[i].address),sites::kSites[i].length,unsigned(patches[i].patched_in),patches[i].status);
    return active.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame,bool sampled,std::uint64_t view_submit_us) noexcept {
    ErrorGuard error;
    if(!gate.admit(GetCurrentThreadId()))return;
    if(!sampled){accumulator.discard();++dropped;return;}
    accumulator.take(frame,frequency,view_submit_us,last_sample);
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
    bool restored=true;
    for(unsigned i=sites::Count;i-->0;){
        if(patches[i].patched_in&&!engine_patch::restore(patches[i]))restored=false;
        patches[i]=engine_patch::Site{};
    }
    return restored;
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
