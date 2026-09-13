#include "chase_transition.h"
#include "chase_transition_core.h"
#include "chase_camera.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "telemetry.h"
#include <atomic>
#include <cstring>

static_assert(sizeof(void*)==4,"Verified x86 game ABI only");
namespace x3m::chase_transition {
namespace {
// All spans are whole instructions, plain-copy relocations. The constructor
// has one successful exit; the updater has two. See the focused site probe.
constexpr engine_patch::SiteSpec specs[] = {
    {"chase_cockpit_construct",0x0041f8d0,{0x6a,0xff,0x68,0xdd,0x07,0x53,0x00},7,0,0},
    {"chase_cockpit_complete",0x0041fe04,{0x8b,0x4c,0x24,0x14,0x5f},5,0,0},
    {"chase_cockpit_destroy",0x0041ffc0,{0x6a,0xff,0x68,0x6b,0x09,0x53,0x00},7,0,0},
    {"chase_update_begin",0x004205e0,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0},
    {"chase_update_end_internal",0x004216c3,{0x5f,0x5e,0x5b,0x8b,0xe5},5,0,0},
    {"chase_update_end",0x004216d3,{0x5f,0x5e,0x5b,0x8b,0xe5},5,0,0},
    {"chase_mode_script",0x0042e742,{0x89,0x88,0x50,0x01,0x00,0x00},6,0,0},
    {"chase_mode_load",0x00419e06,{0x89,0x95,0x50,0x01,0x00,0x00},6,0,0},
    {"chase_connect",0x00422cd0,{0x83,0xec,0x08,0x83,0xf8,0x09},6,0,0}
};
constexpr unsigned mandatory=6, site_count=9;
engine_patch::Site sites[site_count];
std::atomic<bool> enabled{false},diagnostic{false};
bool initialized=false;
SRWLOCK lock=SRWLOCK_INIT;
detail::State state;
struct Guard { Guard(){AcquireSRWLockExclusive(&lock);}~Guard(){ReleaseSRWLockExclusive(&lock);} };
using detail::Origin;
struct Snapshot {
    std::uint32_t valid=0,mode=0,connect=0,ship=0,view=0,camera=0,sector=0,target=0;
    std::int32_t boom[3]{};
};
struct Event {
    std::uint64_t sequence=0,qpc=0,generation=0,update=0;
    std::uint32_t thread=0,kind=0,cockpit=0,caller=0,requested=0,handle=0,valid=0;
    Snapshot before;
    Origin origin;
};
struct Seen { std::uintptr_t cockpit=0;std::uint64_t generation=0;Event event;bool used=false; };
Seen seen[4][detail::lifetime_capacity]{}, unknown_updates[detail::thread_capacity]{};
detail::Window<Event> window;
std::uint64_t sequence=0,read_failures=0,suppressed=0;

bool bytes(std::uintptr_t base,unsigned offset,void* out,unsigned n) {
    if(!base || base>UINT32_MAX || offset>UINT32_MAX-base)return false;
    const auto at=base+offset;
    return n && n<=UINT32_MAX-at && engine_memory::read(at,out,n);
}
template<class T> bool field(std::uintptr_t base,unsigned offset,T& out){return bytes(base,offset,&out,sizeof out);}
std::uint64_t now(){LARGE_INTEGER q{};return QueryPerformanceCounter(&q)&&q.QuadPart>0?std::uint64_t(q.QuadPart):0;}
void snapshot(std::uintptr_t c,Snapshot& s) {
    if(field(c,0x150,s.mode))s.valid|=1;
    if(field(c,0x1c0,s.connect))s.valid|=2;
    if(field(c,0xc,s.ship))s.valid|=4;
    if(field(c,0x10,s.view))s.valid|=8;
    if(field(c,0x58,s.camera))s.valid|=16;
    if(field(c,0x1fc,s.sector))s.valid|=32;
    if(field(c,0x1e0,s.target))s.valid|=64;
    if(field(c,0x130,s.boom))s.valid|=128;
}
bool active_handle(std::uintptr_t cockpit,std::uint32_t& handle) {
    std::uint32_t registry=0,table=0,bucket[2]{},link=0;
    if(!field(0x608504,0,registry)||!field(registry,0,table)||!field(registry,0x10,handle)||!field(table,0,bucket))return false;
    const auto n=bucket[1];
    if(!n||n>65536||(n&(n-1))||!field(bucket[0],4*((n-1)&handle),link))return false;
    for(unsigned i=0;link && i<32;++i){std::uint32_t row[3]{};if(!field(link,0,row))return false;
        if(row[1]==handle)return row[2]==cockpit;
        link=row[0];}
    return false;
}
// Runtime CODE length has not been established. Instead of inventing one,
// enforce overflow-free offsets and the same VirtualAlloc allocation as CODE
// base, then use the ordinary checked reader. This is conservative at split
// allocations and does not establish that arbitrary readable bytes are code.
bool code_address(std::uint32_t code,std::uint32_t offset,unsigned n,std::uintptr_t& at) {
    if(!code||offset>UINT32_MAX-code||!n||n>UINT32_MAX-(code+offset))return false;
    at=code+offset;
    MEMORY_BASIC_INFORMATION a{},b{};
    return VirtualQuery(reinterpret_cast<void*>(code),&a,sizeof a)==sizeof a &&
        VirtualQuery(reinterpret_cast<void*>(at+n-1),&b,sizeof b)==sizeof b &&
        a.State==MEM_COMMIT && b.State==MEM_COMMIT && a.AllocationBase==b.AllocationBase;
}
bool same(const Event& a,const Event& b) {
    return a.kind==b.kind&&a.requested==b.requested&&a.caller==b.caller&&a.valid==b.valid&&a.handle==b.handle&&
        !std::memcmp(&a.before,&b.before,sizeof a.before)&&!std::memcmp(&a.origin,&b.origin,sizeof a.origin);
}
void record(unsigned kind,std::uintptr_t cockpit,std::uint32_t thread,std::uint32_t caller=0,
            std::uint32_t requested=0,std::uint32_t ebp=0) {
    if(!diagnostic.load(std::memory_order_relaxed))return;
    Event e{};e.kind=kind;e.cockpit=std::uint32_t(cockpit);e.thread=thread;e.caller=caller;e.requested=requested;
    auto* life=state.find(cockpit);e.generation=life?life->generation:0;
    e.update=state.current(cockpit,thread).serial;
    // Constructor entry and partial/unknown lifetimes must not read object fields.
    if(life&&life->complete){snapshot(cockpit,e.before);if(active_handle(cockpit,e.handle))e.valid|=1;else e.handle=0;}
    if(kind==6)detail::provenance(ebp,e.origin,bytes,code_address);
    if(kind==3 && !life) {
        Seen* slot=nullptr;
        for(auto& row:unknown_updates)if(row.used&&row.event.thread==thread){slot=&row;break;}
        if(!slot)for(auto& row:unknown_updates)if(!row.used){slot=&row;break;}
        if(!slot){++suppressed;return;}
        if(slot->used&&slot->cockpit==cockpit&&same(slot->event,e)){++suppressed;return;}
        *slot={cockpit,0,e,true};
    }
    if((kind>=3) && life) {
        const auto slot=unsigned(life-state.lives);
        const unsigned category=kind>=6?kind-5:0;
        auto& prior=seen[category][slot];
        if(prior.used&&prior.cockpit==cockpit&&prior.generation==e.generation&&same(prior.event,e)){++suppressed;return;}
        prior={cockpit,e.generation,e,true};
    }
    if(e.origin.flags&1)++read_failures;
    e.sequence=++sequence;e.qpc=now();window.push(e);
}
void handle(unsigned kind,std::uint32_t* regs) {
    if(!enabled.load(std::memory_order_acquire) ||
       (kind>=mandatory && !diagnostic.load(std::memory_order_relaxed)))return;
    const auto thread=GetCurrentThreadId();
    const auto esp=regs[3]+4; // PUSHFD precedes PUSHAD's saved ESP.
    std::uint32_t cockpit=0,caller=0;
    Guard guard;
    switch(kind){
    case 0:
        if(field(esp,4,cockpit)){field(esp,0,caller);state.construct(cockpit,thread);record(0,cockpit,thread,caller);}break;
    case 1:
        cockpit=regs[7];if(state.complete(cockpit,thread))record(1,cockpit,thread);break;
    case 2:
        if(field(esp,4,cockpit)){field(esp,0,caller);record(2,cockpit,thread,caller);state.destroy(cockpit);}break;
    case 3:
        // Even a failed argument read must revoke this thread's previous update.
        field(esp,4,cockpit);state.begin(cockpit,esp,thread);record(3,cockpit,thread);break;
    case 4:case 5:state.end(regs[2]+4,thread);break;
    case 6:record(6,regs[7],thread,0x42e742,regs[6],regs[2]);break;
    case 7:record(7,regs[2],thread,0x419e06,regs[5]);break;
    case 8:field(esp,0,caller);record(8,regs[1],thread,caller,regs[7]);break;
    }
}
}
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_chase_transition_enter(unsigned kind,std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr=0x1f80;asm volatile("ldmxcsr %0" :: "m"(mxcsr):"memory");
    x3m::chase_transition::handle(kind,regs);
}
namespace x3m::chase_transition {
namespace {
void* emit(unsigned kind,void*** next_out) {
    engine_patch::Emitter e(192);if(!e.ok())return nullptr;void* start=e.here();
    e.byte(0x9c);e.byte(0x60);e.byte(0xfc); // preserve DF, provide the C ABI's clear DF
    e.byte(0x81);e.byte(0xec);e.dword(0x80);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x8d);e.byte(0x84);e.byte(0x24);e.dword(0x80);e.byte(0x50);
    e.byte(0x68);e.dword(kind);e.byte(0xe8);e.rel32(reinterpret_cast<const void*>(&x3m_chase_transition_enter));
    e.byte(0x83);e.byte(0xc4);e.byte(8);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x61);e.byte(0x9d);
    const auto next=(reinterpret_cast<std::uintptr_t>(e.here())+6+3)&~std::uintptr_t(3);
    e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));
    while(e.ok()&&reinterpret_cast<std::uintptr_t>(e.here())<next)e.byte(0xcc);
    *next_out=reinterpret_cast<void**>(next);e.dword(0);return e.finish()?start:nullptr;
}
bool restore_group(unsigned first,unsigned end) {
    bool okay=true;for(unsigned i=end;i-->first;)if(sites[i].patched_in&&!engine_patch::restore(sites[i]))okay=false;
    return okay;
}
bool install_group(unsigned first,unsigned end) {
    for(unsigned i=first;i<end;++i){
        if(!engine_patch::claim(sites[i],specs[i])){restore_group(first,end);return false;}
        void** next=nullptr;void* stub=emit(i,&next);
        if(!stub||!next||!engine_patch::store_pointer(next,*sites[i].entry)||!engine_patch::push_front(sites[i],stub)){
            restore_group(first,end);return false;}
    }
    return true;
}
}
bool initialize() {
    const DWORD error=GetLastError();
    if(initialized){SetLastError(error);return installed();}initialized=true;
    const bool wanted=chase_camera::wanted()&&chase_camera::installed();
    const bool okay=wanted&&object_trace::executable_verified()&&engine_patch::install_window_open()&&install_group(0,mandatory);
    enabled.store(okay,std::memory_order_release);
    const bool diag=okay&&telemetry::enabled()&&install_group(mandatory,site_count);
    diagnostic.store(diag,std::memory_order_release);
    log("chase_transition installed=%u diagnostics=%u mandatory_sites=6 diagnostic_sites=3 mode_writes=0 lifetime_capacity=64 thread_capacity=8",unsigned(okay),unsigned(diag));
    for(unsigned i=0;i<site_count;++i)if(sites[i].patched_in||wanted)
        log("chase_transition_site index=%u site=0x%08lx patched=%u status=%s",i,static_cast<unsigned long>(specs[i].address),unsigned(sites[i].patched_in),sites[i].status);
    SetLastError(error);return okay;
}
bool installed(){return enabled.load(std::memory_order_acquire);}
bool diagnostics_active(){return diagnostic.load(std::memory_order_acquire);}
std::uint64_t generation(std::uintptr_t cockpit) noexcept {
    if(!installed())return 0;
    const DWORD error=GetLastError();std::uint64_t result;
    {Guard guard;result=state.generation(cockpit);}SetLastError(error);return result;
}
Update current_update(std::uintptr_t cockpit) noexcept {
    if(!installed())return {};
    const DWORD error=GetLastError();Update result;
    {Guard guard;result=state.current(cockpit,GetCurrentThreadId());}SetLastError(error);return result;
}
void report(std::uint64_t frame) {
    if(!installed())return;
    detail::Window<Event> out;std::uint64_t over=0,threads=0,failed=0,skipped=0;
    {Guard guard;out=window;window={};over=state.lifetime_overflow;threads=state.thread_overflow;failed=read_failures;skipped=suppressed;}
    if(!diagnostics_active())return;
    log("chase_transition_window frame=%llu first=%u last=%u dropped=%llu lifetime_overflow=%llu thread_overflow=%llu origin_refusals_total=%llu suppressed_total=%llu",frame,out.first_used,out.last_used,out.dropped,over,threads,failed,skipped);
    auto print=[](const Event& e){
        log("chase_transition_event event=%llu qpc=%llu generation=%llu update=%llu thread=%lu kind=%lu cockpit=0x%08lx caller=0x%08lx requested=%lu valid=%lu active_handle=%lu snapshot_valid=%lu mode=%lu connect=%lu ship=0x%08lx view=0x%08lx camera=0x%08lx sector=0x%08lx target=0x%08lx boom=%ld,%ld,%ld origin_valid=%lu origin_flags=%lu task=0x%08lx next_pc=0x%08lx method=0x%08lx entry=0x%08lx ancestry_count=%u ancestry=%08lx:%08lx,%08lx:%08lx,%08lx:%08lx,%08lx:%08lx",
            e.sequence,e.qpc,e.generation,e.update,e.thread,e.kind,e.cockpit,e.caller,e.requested,e.valid,e.handle,
            e.before.valid,e.before.mode,e.before.connect,e.before.ship,e.before.view,e.before.camera,e.before.sector,e.before.target,
            e.before.boom[0],e.before.boom[1],e.before.boom[2],e.origin.valid,e.origin.flags,e.origin.task,e.origin.pc,e.origin.method,e.origin.entry,e.origin.count,
            e.origin.methods[0],e.origin.returns[0],e.origin.methods[1],e.origin.returns[1],e.origin.methods[2],e.origin.returns[2],e.origin.methods[3],e.origin.returns[3]);
    };
    for(unsigned i=0;i<out.first_used;++i)print(out.first[i]);
    const unsigned start=out.last_used==32?out.next:0;
    for(unsigned i=0;i<out.last_used;++i)print(out.last[(start+i)%32]);
}
void shutdown() {
    diagnostic.store(false,std::memory_order_release);enabled.store(false,std::memory_order_release);
    const bool diag=restore_group(mandatory,site_count),base=restore_group(0,mandatory);
    {Guard guard;for(auto& t:state.threads)t={};for(auto& l:state.lives)l={};}
    log("chase_transition_shutdown mandatory_restored=%u diagnostic_restored=%u",unsigned(base),unsigned(diag));
}
}
