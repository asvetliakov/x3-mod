#include "chase_transition.h"
#include "chase_transition_core.h"
#include "chase_transition_identity_core.h"
#include "chase_transition_restore_core.h"
#include "chase_camera.h"
#include "chase_lead.h"
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
// X3M_CHASE_VIEW_RESTORE=1 only (docs/reverse-engineering/chase-view-transition.md,
// run60 lifecycle table): the shared optimized store seam plus six cancellation
// boundaries. All plain whole-instruction copies; the seam's displaced ADD/CMP
// replay after the stub restores GPR/flags, so their live flags are fresh.
constexpr engine_patch::SiteSpec restore_specs[] = {
    {"chase_restore_store",0x004a3ffd,{0x03,0x70,0x0c,0x80,0x3e,0x08},6,0,0},
    {"chase_restore_task_complete",0x004a2260,{0x53,0x8b,0x5c,0x24,0x0c},5,0,0},
    {"chase_restore_task_abort",0x004a2420,{0x53,0x8b,0x5c,0x24,0x0c},5,0,0},
    {"chase_restore_vm_construct",0x0049c9a0,{0x53,0x33,0xdb,0x89,0x5e,0x04},6,0,0},
    {"chase_restore_vm_clear",0x0049ea80,{0x83,0xec,0x08,0x55,0x8b,0x6c,0x24,0x10},8,0,0},
    {"chase_restore_vm_load",0x004a0880,{0x6a,0xff,0x68,0xc8,0x00,0x53,0x00},7,0,0},
    {"chase_restore_eh_adapter",0x0052f298,{0xb8,0xf4,0xe5,0x56,0x00},5,0,0}
};
constexpr unsigned restore_site_count=7, restore_eh_kind=6;
engine_patch::Site sites[site_count],restore_sites[restore_site_count];
std::atomic<bool> enabled{false},diagnostic{false},restore_enabled{false};
bool initialized=false,timing=false;
std::uint64_t frequency=0;
detail::HandlerTiming handler_timing[site_count]{};
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
    struct Geometry { std::uint32_t valid=0,lock=0;std::int32_t angles[3]{},offset[3]{}; } geometry;
    detail::Identity identity;
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
// Process roots; the CPU fixture aliases them to fixture-owned memory.
std::uint32_t vm_root=0x6085e4,native_registry_root=0x60850c,cockpit_registry_root=0x608504;
bool active_handle(std::uintptr_t cockpit,std::uint32_t& handle) {
    std::uint32_t registry=0,table=0,bucket[2]{},link=0;
    if(!field(cockpit_registry_root,0,registry)||!field(registry,0,table)||!field(registry,0x10,handle)||!field(table,0,bucket))return false;
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
    if(kind==2)detail::destructor_provenance(caller,ebp,e.origin,bytes,code_address);
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
    // Supplement only an admitted event. Animated angles and identity walks
    // neither change suppression nor add work on every updater/draw.
    if(life&&life->complete){
        if(field(cockpit,0xa8,e.geometry.angles))e.geometry.valid|=1;
        if(field(cockpit,0x160,e.geometry.offset))e.geometry.valid|=2;
        if(field(cockpit,0x120,e.geometry.lock))e.geometry.valid|=4;
    }
    detail::IdentityReader<decltype(&bytes)> identity_reader{&bytes};
    identity_reader.capture((e.before.valid&4)?e.before.ship:0,
        (e.origin.valid&7)==7?e.origin.context:0,e.identity);
    if(e.origin.flags&1)++read_failures;
    e.sequence=++sequence;e.qpc=now();window.push(e);
}
// ---- one-use rear-chase restore ticket (X3M_CHASE_VIEW_RESTORE=1) ----
// The lock-protected state; the epoch is lock-free so the EH adapter can
// cancel without acquiring anything. The filter words are read racily by the
// store stub (mode 0 idle, 1 armed: operand addresses, 2 pending: also global
// slot 8/9 stores); the full path re-validates everything under the lock.
detail::RestoreState restore;
std::atomic<std::uint32_t> restore_epoch{1};
std::atomic<std::uint32_t> restore_filter[1+detail::restore_filter_count]{};
std::atomic<std::uint64_t> restore_eh_bumps{0};
detail::HandlerTiming restore_timing[restore_site_count]{};
constexpr unsigned restore_pending_update_expiry=600;
// Once-per-reason arm/transfer refusal samples, captured under the lock and
// emitted by handle() after it is released (log() must not run under the Guard).
struct RestoreSample { bool wanted=false,armed=false;unsigned kind=0,reason=0,readable=0;std::uint32_t cockpit=0,mode=0,connect=0,ship=0,view=0,handle=0,valid=0,refused=0,native_script=0,player=0;std::uint64_t generation=0; };
RestoreSample restore_sample;
// readable: bit0 connect, bit1 ship, bit2 view field reads succeeded (a real 0 is distinguishable from an unreadable field).
void restore_sample_arm(unsigned reason,std::uintptr_t cockpit,std::uint64_t gen,std::uint32_t mode,unsigned readable,std::uint32_t connect,std::uint32_t ship,std::uint32_t view,std::uint32_t handle,const detail::Identity* id) {
    if(reason>=detail::arm_refusal_count)reason=0;
    ++restore.arm_refusal_reasons[reason];
    const std::uint32_t bit=1u<<reason;if(restore.arm_sample_logged&bit)return;restore.arm_sample_logged|=bit;
    restore_sample={};restore_sample.wanted=true;restore_sample.kind=0;restore_sample.reason=reason;restore_sample.readable=readable;restore_sample.armed=restore.armed;
    restore_sample.cockpit=std::uint32_t(cockpit);restore_sample.generation=gen;
    restore_sample.mode=mode;restore_sample.connect=connect;restore_sample.ship=ship;restore_sample.view=view;restore_sample.handle=handle;
    if(id){restore_sample.valid=id->valid;restore_sample.refused=id->refused;restore_sample.native_script=id->native_script;restore_sample.player=id->player;}
}
void restore_sample_transfer(unsigned reason,std::uintptr_t cockpit,std::uint64_t gen,const Origin& o) {
    if(reason>=detail::refuse_count)reason=0;
    const std::uint32_t bit=1u<<reason;if(restore.transfer_sample_logged&bit)return;restore.transfer_sample_logged|=bit;
    restore_sample={};restore_sample.wanted=true;restore_sample.kind=1;restore_sample.armed=restore.armed;restore_sample.reason=reason;restore_sample.cockpit=std::uint32_t(cockpit);restore_sample.generation=gen;
    restore_sample.valid=o.valid;restore_sample.refused=o.flags;restore_sample.handle=o.count;restore_sample.ship=o.task;restore_sample.view=o.context;
}
RestoreSample restore_take_sample(){RestoreSample s=restore_sample;restore_sample.wanted=false;return s;}
void restore_emit_sample(const RestoreSample& s) {
    if(s.kind==0)log("chase_view_restore_arm_refused reason=%u cockpit=0x%08lx generation=%llu mode=%lu readable=%u connect=%lu ship=0x%08lx view=0x%08lx handle=%lu identity_valid=%lu identity_refused=%lu native_script=0x%08lx player=0x%08lx armed=%u",
        s.reason,static_cast<unsigned long>(s.cockpit),s.generation,static_cast<unsigned long>(s.mode),s.readable,static_cast<unsigned long>(s.connect),static_cast<unsigned long>(s.ship),static_cast<unsigned long>(s.view),
        static_cast<unsigned long>(s.handle),static_cast<unsigned long>(s.valid),static_cast<unsigned long>(s.refused),static_cast<unsigned long>(s.native_script),static_cast<unsigned long>(s.player),unsigned(s.armed));
    else log("chase_view_restore_transfer_refused reason=%u cockpit=0x%08lx generation=%llu origin_valid=%lu origin_flags=%lu context_return_count=%lu task=0x%08lx context=0x%08lx",
        s.reason,static_cast<unsigned long>(s.cockpit),s.generation,static_cast<unsigned long>(s.valid),static_cast<unsigned long>(s.refused),static_cast<unsigned long>(s.handle),static_cast<unsigned long>(s.ship),static_cast<unsigned long>(s.view));
}
void restore_filter_publish() {
    const std::uint32_t pcs[detail::restore_filter_count]={detail::restore_mode_pc,detail::restore_player_pc,
        detail::restore_controller_pc,detail::restore_killed_pcs[0],detail::restore_killed_pcs[1]};
    const std::uint32_t code=restore.armed||restore.pending?restore.arm_code:0;
    for(unsigned i=0;i<detail::restore_filter_count;++i)restore_filter[1+i].store(code?code+pcs[i]+1:0,std::memory_order_relaxed);
    restore_filter[0].store(restore.filter_mode(),std::memory_order_release);
}
bool restore_epoch_ok() {
    const auto e=restore_epoch.load(std::memory_order_acquire);
    // An epoch cancellation (EH adapter) must let a later admitted rear update re-arm.
    if(restore.armed&&restore.arm_epoch!=e){restore.clear_arm(detail::cancel_epoch);restore.reset_attempt();}
    if(restore.pending&&restore.pending_epoch!=e){restore.clear_pending(detail::cancel_epoch);restore.reset_attempt();}
    return restore.armed||restore.pending;
}
bool writable_span(std::uintptr_t at,unsigned n) {
    constexpr DWORD writable=PAGE_READWRITE|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY;
    MEMORY_BASIC_INFORMATION a{},b{};
    if(!at||!n||n>UINT32_MAX-at)return false;
    return VirtualQuery(reinterpret_cast<void*>(at),&a,sizeof a)==sizeof a&&VirtualQuery(reinterpret_cast<void*>(at+n-1),&b,sizeof b)==sizeof b&&
        a.State==MEM_COMMIT&&b.State==MEM_COMMIT&&!(a.Protect&PAGE_GUARD)&&!(b.Protect&PAGE_GUARD)&&(a.Protect&writable)&&(b.Protect&writable);
}
bool write_payload(std::uintptr_t at,std::uint32_t value) {
    SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&value,4,&written)&&written==4;
}
// Arm from an admitted rear-chase update: one identity walk per lifetime and
// per re-entry into mode 258, never per frame while armed.
void restore_on_update(std::uintptr_t cockpit,std::uint32_t thread) {
    (void)thread;
    if(!restore_enabled.load(std::memory_order_relaxed))return;
    const auto gen=state.generation(cockpit);
    std::uint32_t mode=0;
    if(!gen||!field(cockpit,0x150,mode))return;
    restore_epoch_ok();
    if(restore.pending){
        if(++restore.pending_updates>restore_pending_update_expiry){restore.clear_pending(detail::cancel_expiry);restore_filter_publish();}
        return;
    }
    if(restore.armed){
        if(restore.arm_cockpit==cockpit&&restore.arm_generation==gen&&mode!=detail::restore_rear_mode){
            restore.clear_arm(detail::cancel_mode_left);restore.attempt_generation=gen;restore.attempt_mode=mode;restore.retries=0;restore_filter_publish();}
        return;
    }
    if(restore.attempt_generation==gen&&restore.attempt_mode==mode)return;
    if(mode!=detail::restore_rear_mode){restore.attempt_generation=gen;restore.attempt_mode=mode;restore.retries=0;return;}
    // Preconditions are re-checked on rear updates (run68: a fresh generation's
    // first updates carry view 0). Per-update cost on this path: the mode read
    // above plus up to three `field` reads (each one engine_memory read: a
    // cached VirtualQuery region check and copy in direct mode, one
    // NtReadVirtualMemory in rpm mode); active_handle's bounded registry walk
    // runs only once connect and view pass. Retries are capped per cockpit
    // generation; exhaustion records the attempt so a persistent refusal costs
    // nothing further for that lifetime and mode.
    if(restore.retry_generation!=gen){restore.retry_generation=gen;restore.retries=0;}
    if(restore.retries>=detail::restore_arm_retry_cap){
        restore.attempt_generation=gen;restore.attempt_mode=mode;++restore.arm_refusals;
        restore_sample_arm(detail::arm_retry_exhausted,cockpit,gen,mode,0,0,0,0,0,nullptr);return;
    }
    ++restore.retries;
    std::uint32_t connect=0,ship=0,view=0,handle=0,code=0;unsigned readable=0;
    if(field(cockpit,0x1c0,connect))readable|=1;
    if(!(readable&1)||connect){++restore.arm_refusals;restore_sample_arm(detail::arm_connect,cockpit,gen,mode,readable,connect,0,0,0,nullptr);return;}
    if(field(cockpit,0xc,ship))readable|=2;
    if(field(cockpit,0x10,view))readable|=4;
    if(readable!=7||!ship||(ship&3)||view!=ship){++restore.arm_refusals;restore_sample_arm(detail::arm_view_not_ready,cockpit,gen,mode,readable,connect,ship,view,0,nullptr);return;}
    if(!active_handle(cockpit,handle)){++restore.arm_refusals;restore_sample_arm(detail::arm_no_handle,cockpit,gen,mode,readable,connect,ship,view,handle,nullptr);return;}
    // The identity walk is the expensive step: one attempt per lifetime and mode entry.
    restore.attempt_generation=gen;restore.attempt_mode=mode;
    detail::IdentityReader<decltype(&bytes)> reader{&bytes};reader.vm_root=vm_root;reader.registry_root=native_registry_root;
    detail::Identity id;reader.capture(ship,0,id);
    using I=detail::Identity;
    constexpr std::uint32_t need=I::vm_bit|I::native_bit|I::player_bit|I::controller_bit|I::warp_bit|I::killed_bit;
    if((id.valid&need)!=need||!id.player||id.native_script!=id.player||id.killed){++restore.arm_refusals;restore_sample_arm(detail::arm_identity,cockpit,gen,mode,readable,connect,ship,view,handle,&id);return;}
    if(!field(id.vm,8,code)||!code){++restore.arm_refusals;restore_sample_arm(detail::arm_code,cockpit,gen,mode,readable,connect,ship,view,handle,&id);return;}
    restore.armed=true;restore.arm_cockpit=std::uint32_t(cockpit);restore.arm_generation=gen;restore.arm_player=id.player;
    restore.arm_controller=id.controller;restore.arm_native_script=id.native_script;restore.arm_code=code;
    restore.arm_epoch=restore_epoch.load(std::memory_order_acquire);++restore.arms;
    restore_filter_publish();
}
// Transfer to pending only at the measured warp destructor; any other
// destructor clears the arm and a second destruction clears pending.
void restore_on_destroy(std::uintptr_t cockpit,std::uint32_t caller,std::uint32_t ebp,std::uint32_t thread) {
    if(!restore_enabled.load(std::memory_order_relaxed))return;
    restore_epoch_ok();
    if(restore.pending){restore.clear_pending(detail::cancel_second_destruction);restore_filter_publish();return;}
    if(!restore.armed)return;
    const auto* life=state.find(cockpit);
    const std::uint64_t gen=life?life->generation:0;
    if(cockpit!=restore.arm_cockpit||gen!=restore.arm_generation||caller!=detail::restore_destructor_caller){
        restore.clear_arm(detail::cancel_destructor);restore.reset_attempt();restore_filter_publish();return;}
    Origin o;detail::destructor_provenance(caller,ebp,o,bytes,code_address,vm_root);
    std::uint32_t monitor=0,task_id=0;
    const unsigned why=detail::transfer_proof(restore,o,bytes,vm_root,monitor,task_id);
    if(why){restore.refuse(why);restore_sample_transfer(why,cockpit,gen,o);restore.clear_arm(detail::cancel_destructor);restore.reset_attempt();restore_filter_publish();return;}
    restore.transfer(monitor,o.task,task_id,thread,restore_epoch.load(std::memory_order_acquire));
    restore_filter_publish();
}
// Whether a SelectMode assignment belongs to the armed/pending main monitor.
// Pending admits only the fresh monitor identity captured at the destructor
// prefix; armed admits the monitor whose ref cell17 is the player (run65:
// cell11 is a camera priority). A validated foreign monitor, including one
// whose cell17 is unset/unreadable (side monitors never set it), is ignored;
// only an unreadable/foreign-class context itself is malformed and cancels.
bool restore_store_is_ours(std::uint32_t esp,std::uint32_t eax,bool& malformed) {
    std::uint32_t context=0,id=0,ref=0,tag=0,g9_tag=0,desc[14]{};
    detail::IdentityReader<decltype(&bytes)> r{&bytes};r.vm_root=vm_root;r.registry_root=native_registry_root;
    malformed=!field(esp,0x20,context)||context!=eax||!r.root()||!detail::borrowed_context(r,context,detail::restore_monitor_class,id,desc);
    if(malformed)return true;
    if(restore.pending)return id==restore.pending_monitor;
    return detail::global9_tag(r,g9_tag)&&detail::raw_cell(r,context,desc,17,tag,ref)&&detail::ref_tag_ok(tag,g9_tag)&&ref==restore.arm_player;
}
// One bounded record per seam call while pending (rare: between the warp
// destructor and the reset), so the next run measures every proof input.
// Captured under the lock, emitted by restore_handle after the lock is
// released: log() takes capture's mutex, which present() holds while calling
// report() under this same lock.
struct SeamLog {
    bool wanted=false,ours=false,ctx=false,src=false,prefix=false;unsigned readable=0,why=0;
    std::uint32_t pc=0,opcode=0,index=0,esi=0,context=0,id=0,pending_monitor=0,v[4]{},t[4]{},requested=0,epoch=0;unsigned char source_tag=0;
};
void restore_capture_seam(const detail::SeamRegs& s,const detail::SeamDecode& d,bool ours,unsigned why,SeamLog& out) {
    std::uint32_t desc[14]{};unsigned char source[5]{};
    detail::IdentityReader<decltype(&bytes)> r{&bytes};r.vm_root=vm_root;r.registry_root=native_registry_root;
    out.wanted=true;out.ours=ours;out.why=why;out.pc=d.pc;out.opcode=d.opcode;out.index=d.index;out.esi=s.esi;out.pending_monitor=restore.pending_monitor;
    out.ctx=field(s.esp,0x20,out.context)&&r.root()&&detail::borrowed_context(r,out.context,detail::restore_monitor_class,out.id,desc);
    const unsigned index[4]={0,1,16,17};
    if(out.ctx)for(unsigned i=0;i<4;++i)if(detail::raw_cell(r,out.context,desc,index[i],out.t[i],out.v[i]))out.readable|=1u<<i;
    out.src=bytes(s.ebx,0,source,5);if(out.src){out.source_tag=source[0];std::memcpy(&out.requested,source+1,4);}
    out.prefix=d.task&&detail::live_stack_prefix(d.task,s.ebx,d.code,bytes,code_address,detail::restore_reset_prefix,3);
    out.epoch=restore_epoch.load(std::memory_order_relaxed);
}
void restore_emit_seam(const SeamLog& o) {
    log("chase_view_restore_seam pc=0x%lx opcode=0x%lx index=%lu esi=%lu context=0x%08lx monitor=0x%08lx pending_monitor=0x%08lx ours=%u cells_readable=%u cell0=%lu cell1=%lu cell16=%lu cell17_tag=%lu cell17=0x%08lx cell0_tag=%lu cell1_tag=%lu cell16_tag=%lu source_readable=%u source_tag=%u source=%lu prefix_ok=%u refusal=%u epoch=%lu",
        static_cast<unsigned long>(o.pc),static_cast<unsigned long>(o.opcode),static_cast<unsigned long>(o.index),static_cast<unsigned long>(o.esi),
        static_cast<unsigned long>(o.context),static_cast<unsigned long>(o.id),static_cast<unsigned long>(o.pending_monitor),unsigned(o.ours),o.readable,
        static_cast<unsigned long>(o.v[0]),static_cast<unsigned long>(o.v[1]),static_cast<unsigned long>(o.v[2]),static_cast<unsigned long>(o.t[3]),static_cast<unsigned long>(o.v[3]),
        static_cast<unsigned long>(o.t[0]),static_cast<unsigned long>(o.t[1]),static_cast<unsigned long>(o.t[2]),unsigned(o.src),unsigned(o.source_tag),static_cast<unsigned long>(o.requested),
        unsigned(o.prefix),o.why,static_cast<unsigned long>(o.epoch));
}
void restore_on_store(std::uint32_t* regs,std::uint32_t esp,std::uint32_t thread,SeamLog& seam_log) {
    ++restore.seam_calls;
    if(!restore_epoch_ok()){restore_filter_publish();return;}
    detail::SeamRegs s;s.eax=regs[7];s.ebx=regs[4];s.esi=regs[1];s.edi=regs[0];s.ebp=regs[2];s.esp=esp;s.thread=thread;
    detail::SeamDecode d;
    if(!detail::seam_decode(s,bytes,code_address,vm_root,d)){restore.refuse(detail::refuse_opcode);restore.clear_all(detail::cancel_seam_unreadable);restore.reset_attempt();restore_filter_publish();return;}
    if(d.pc==detail::restore_mode_pc){
        bool malformed=false;
        const bool ours=restore_store_is_ours(esp,s.eax,malformed);
        if(restore.pending&&!ours){restore_capture_seam(s,d,false,0,seam_log);return;}
        if(!ours)return;
        if(restore.pending){
            unsigned why=detail::seam_consume_proof(restore,s,d,restore_epoch.load(std::memory_order_acquire),bytes,code_address,vm_root);
            if(!why&&!writable_span(s.ebx+1,4))why=detail::refuse_writable;
            restore_capture_seam(s,d,true,why,seam_log);
            if(why){restore.refuse(why);restore.clear_pending(detail::cancel_proof);restore_filter_publish();return;}
            // Clear pending immediately before the single source-payload write.
            restore.take_pending();restore_filter_publish();
            if(write_payload(s.ebx+1,detail::restore_rear_mode))++restore.consumed;
            else{++restore.writes_failed;restore.refuse(detail::refuse_write);}
            return;
        }
        restore.clear_arm(detail::cancel_selection);restore.reset_attempt();restore_filter_publish();return;
    }
    if(d.pc==detail::restore_player_pc||d.pc==detail::restore_controller_pc){
        restore.clear_all(detail::cancel_identity_store);restore.reset_attempt();restore_filter_publish();return;}
    if(d.pc==detail::restore_killed_pcs[0]||d.pc==detail::restore_killed_pcs[1]){
        if(!detail::killed_store_is_zero(s,d,bytes,vm_root)){restore.clear_all(detail::cancel_killed_store);restore.reset_attempt();restore_filter_publish();}
        return;
    }
    if(restore.pending)restore_capture_seam(s,d,false,0,seam_log);
    if(restore.pending&&d.opcode==0x93&&(s.esi==40||s.esi==45)){restore.clear_pending(detail::cancel_global_slot);restore_filter_publish();}
}
void restore_on_task(unsigned kind,std::uint32_t task) {
    if(restore.pending&&(!task||task==restore.pending_task)){
        restore.clear_pending(kind==1?detail::cancel_task_complete:detail::cancel_task_abort);restore_filter_publish();}
}
void restore_on_vm() {
    restore_epoch.fetch_add(1,std::memory_order_acq_rel);
    restore.clear_all(detail::cancel_epoch);restore.attempt_generation=0;restore.reset_attempt();restore_filter_publish();
}
// Deserialization kind7 (the existing mode-load observer, 0x419e06) is an
// additional cancel: same epoch advance as the VM boundaries, no new site.
void restore_on_load() {
    if(!restore_enabled.load(std::memory_order_relaxed))return;
    restore_on_vm();
}
void restore_handle(unsigned kind,std::uint32_t* regs) {
    if(kind>=restore_site_count||!restore_enabled.load(std::memory_order_acquire))return;
    if(kind==restore_eh_kind){
        // Lock-free: native unwinding may pass through this adapter while
        // another observer holds its lock on the same or another thread.
        restore_epoch.fetch_add(1,std::memory_order_acq_rel);restore_eh_bumps.fetch_add(1,std::memory_order_relaxed);return;
    }
    const auto start=timing?now():0;
    const auto thread=GetCurrentThreadId();
    const auto esp=regs[3]+4;
    SeamLog seam_log;
    {
    Guard guard;
    switch(kind){
    case 0:restore_on_store(regs,esp,thread,seam_log);break;
    case 1:case 2:{std::uint32_t task=0;field(esp,8,task);restore_on_task(kind,task);}break;
    case 3:case 4:case 5:restore_on_vm();break;
    }
    }
    if(seam_log.wanted)restore_emit_seam(seam_log); // never under the Guard
    if(timing){const auto end=now();Guard guard;restore_timing[kind].add(start,end);}
}
void handle(unsigned kind,std::uint32_t* regs) {
    if(kind>=site_count || !enabled.load(std::memory_order_acquire) ||
       (kind>=mandatory && !diagnostic.load(std::memory_order_relaxed)))return;
    const auto start=timing?now():0;
    const auto thread=GetCurrentThreadId();
    const auto esp=regs[3]+4; // PUSHFD precedes PUSHAD's saved ESP.
    std::uint32_t cockpit=0,caller=0;
    RestoreSample sample;
    {
    Guard guard;
    switch(kind){
    case 0:
        // Reentry retires this thread even if the constructor argument is unreadable.
        chase_lead::native_timing_invalidate(0,thread);
        if(field(esp,4,cockpit)){field(esp,0,caller);chase_lead::native_timing_invalidate(cockpit,thread);state.construct(cockpit,thread);record(0,cockpit,thread,caller);}break;
    case 1:
        cockpit=regs[7];if(state.complete(cockpit,thread))record(1,cockpit,thread);break;
    case 2:
        chase_lead::native_timing_invalidate(0,thread);
        if(field(esp,4,cockpit)){field(esp,0,caller);record(2,cockpit,thread,caller,0,regs[2]);restore_on_destroy(cockpit,caller,regs[2],thread);chase_lead::native_timing_invalidate(cockpit,thread);state.destroy(cockpit);}break;
    case 3:
        // Even a failed argument read must revoke this thread's previous update.
        chase_lead::native_timing_invalidate(0,thread);
        field(esp,4,cockpit);state.begin(cockpit,esp,thread);record(3,cockpit,thread);restore_on_update(cockpit,thread);break;
    case 4:case 5:chase_lead::native_timing_invalidate(0,thread);state.end(regs[2]+4,thread);break;
    case 6:record(6,regs[7],thread,0x42e742,regs[6],regs[2]);break;
    case 7:record(7,regs[2],thread,0x419e06,regs[5]);restore_on_load();break;
    case 8:field(esp,0,caller);record(8,regs[1],thread,caller,regs[7]);break;
    }
    sample=restore_take_sample();
    }
    if(sample.wanted)restore_emit_sample(sample); // never under the Guard
    // Include the handler lock acquisition/release and checked reads. The
    // second lock only aggregates this sample and lies outside its interval.
    if(timing){const auto end=now();Guard guard;handler_timing[kind].add(start,end);}
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
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_chase_restore_enter(unsigned kind,std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr=0x1f80;asm volatile("ldmxcsr %0" :: "m"(mxcsr):"memory");
    x3m::chase_transition::restore_handle(kind,regs);
}
namespace x3m::chase_transition {
namespace {
// Register-saving stub. With `filter` (the store seam) a prefilter runs under
// the saved flags before any XMM/x87 capture: idle mode skips at one memory
// compare; armed mode admits only the published operand addresses; pending
// mode also admits optimized global stores (opcode 93) to slots 8/9. The
// opcode byte at EDI-1 was fetched by the interpreter's own dispatch.
void* emit_stub(unsigned kind,const void* callback,void*** next_out,const std::atomic<std::uint32_t>* filter) {
    engine_patch::Emitter e(320);if(!e.ok())return nullptr;void* start=e.here();
    e.byte(0x9c); // pushfd
    const unsigned char* full_at=nullptr;const unsigned char* skip_at=nullptr;
    if(filter){
        constexpr unsigned prefilter=13+12*detail::restore_filter_count+13+10+9+9+5,full_path=116;
        full_at=static_cast<const unsigned char*>(e.here())+prefilter;skip_at=full_at+full_path;
        const auto word=[&](unsigned i){return std::uint32_t(reinterpret_cast<std::uintptr_t>(filter+i));};
        e.byte(0x83);e.byte(0x3d);e.dword(word(0));e.byte(0);e.byte(0x0f);e.byte(0x84);e.rel32(skip_at); // cmp [mode],0; je skip
        for(unsigned i=0;i<detail::restore_filter_count;++i){e.byte(0x3b);e.byte(0x3d);e.dword(word(1+i));e.byte(0x0f);e.byte(0x84);e.rel32(full_at);} // cmp edi,[addr]; je full
        e.byte(0x83);e.byte(0x3d);e.dword(word(0));e.byte(2);e.byte(0x0f);e.byte(0x85);e.rel32(skip_at); // cmp [mode],2; jne skip
        e.byte(0x80);e.byte(0x7f);e.byte(0xff);e.byte(0x93);e.byte(0x0f);e.byte(0x85);e.rel32(skip_at); // cmp byte [edi-1],0x93; jne skip
        e.byte(0x83);e.byte(0xfe);e.byte(40);e.byte(0x0f);e.byte(0x84);e.rel32(full_at); // cmp esi,40; je full
        e.byte(0x83);e.byte(0xfe);e.byte(45);e.byte(0x0f);e.byte(0x84);e.rel32(full_at); // cmp esi,45; je full
        e.byte(0xe9);e.rel32(skip_at);
        if(e.here()!=full_at)return nullptr;
    }
    e.byte(0x60);e.byte(0xfc); // pushad; provide the C ABI's clear DF (DF itself is in the saved flags)
    e.byte(0x81);e.byte(0xec);e.dword(0x80);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x8d);e.byte(0x84);e.byte(0x24);e.dword(0x80);e.byte(0x50);
    e.byte(0x68);e.dword(kind);e.byte(0xe8);e.rel32(callback);
    e.byte(0x83);e.byte(0xc4);e.byte(8);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x61);
    if(filter&&e.here()!=skip_at)return nullptr;
    e.byte(0x9d); // popfd
    const auto next=(reinterpret_cast<std::uintptr_t>(e.here())+6+3)&~std::uintptr_t(3);
    e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));
    while(e.ok()&&reinterpret_cast<std::uintptr_t>(e.here())<next)e.byte(0xcc);
    *next_out=reinterpret_cast<void**>(next);e.dword(0);return e.finish()?start:nullptr;
}
void* emit(unsigned kind,void*** next_out){return emit_stub(kind,reinterpret_cast<const void*>(&x3m_chase_transition_enter),next_out,nullptr);}
void* emit_restore(unsigned kind,void*** next_out){
    return emit_stub(kind,reinterpret_cast<const void*>(&x3m_chase_restore_enter),next_out,kind==0?restore_filter:nullptr);
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
// Partial-install rollback: a refused/failed site restores every earlier one.
bool restore_sites_restore() {
    bool okay=true;for(unsigned i=restore_site_count;i-->0;)if(restore_sites[i].patched_in&&!engine_patch::restore(restore_sites[i]))okay=false;
    return okay;
}
bool restore_sites_install(const engine_patch::SiteSpec* table) {
    for(unsigned i=0;i<restore_site_count;++i){
        if(!engine_patch::claim(restore_sites[i],table[i])){restore_sites_restore();return false;}
        void** next=nullptr;void* stub=emit_restore(i,&next);
        if(!stub||!next||!engine_patch::store_pointer(next,*restore_sites[i].entry)||!engine_patch::push_front(restore_sites[i],stub)){
            restore_sites_restore();return false;}
    }
    return true;
}
bool restore_wanted() {
    wchar_t setting[8]{};
    return GetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE",setting,8)==1&&setting[0]==L'1';
}
}
bool initialize() {
    const DWORD error=GetLastError();
    if(initialized){SetLastError(error);return installed();}initialized=true;
    const bool wanted=chase_camera::wanted()&&chase_camera::installed();
    const bool okay=wanted&&object_trace::executable_verified()&&engine_patch::install_window_open()&&install_group(0,mandatory);
    LARGE_INTEGER f{};
    timing=okay&&telemetry::enabled()&&QueryPerformanceFrequency(&f)&&f.QuadPart>0;
    frequency=timing?std::uint64_t(f.QuadPart):0;
    enabled.store(okay,std::memory_order_release);
    const bool diag=okay&&telemetry::enabled()&&install_group(mandatory,site_count);
    diagnostic.store(diag,std::memory_order_release);
    log("chase_transition installed=%u diagnostics=%u mandatory_sites=6 diagnostic_sites=3 mode_writes=0 lifetime_capacity=64 thread_capacity=8 timing=%u qpc_frequency=%llu identity_version=1 origin_pairs=6 identity_scope=admitted_events",unsigned(okay),unsigned(diag),unsigned(timing),frequency);
    for(unsigned i=0;i<site_count;++i)if(sites[i].patched_in||wanted)
        log("chase_transition_site index=%u site=0x%08lx patched=%u status=%s",i,static_cast<unsigned long>(specs[i].address),unsigned(sites[i].patched_in),sites[i].status);
    // Default off: with the option unset nothing below is claimed and the
    // interpreter operands are never read or written.
    const bool restore_requested=restore_wanted();
    const bool restore_okay=restore_requested&&okay&&engine_patch::install_window_open()&&restore_sites_install(restore_specs);
    {Guard guard;restore={};restore_filter_publish();}
    restore_enabled.store(restore_okay,std::memory_order_release);
    if(restore_requested||restore_okay){
        log("chase_view_restore installed=%u requested=%u sites=%u mutation=source_payload_258_at_0x%lx prefilter=armed_operand_addresses expiry_updates=%u",
            unsigned(restore_okay),unsigned(restore_requested),restore_site_count,static_cast<unsigned long>(detail::restore_mode_pc),restore_pending_update_expiry);
        for(unsigned i=0;i<restore_site_count;++i)
            log("chase_view_restore_site index=%u site=0x%08lx patched=%u status=%s",i,static_cast<unsigned long>(restore_specs[i].address),unsigned(restore_sites[i].patched_in),restore_sites[i].status);
    }
    SetLastError(error);return okay;
}
bool restore_installed(){return restore_enabled.load(std::memory_order_acquire);}
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
bool active_control_cockpit(std::uintptr_t cockpit) noexcept {
    if(!installed()||!cockpit||(cockpit&3))return false;
    const DWORD error=GetLastError();std::uint64_t gen=0;std::uint32_t handle=0;
    // The generation is sampled under the lock; the bounded registry walk then
    // runs unlocked (checked engine reads only). A destroy between the two is
    // excluded only because both run on the game thread, inside the cockpit
    // update whose lifetime witness produced the generation.
    {Guard guard;gen=state.generation(cockpit);}
    // handle != 0: a registry with no active control (slot +0x10 zero) must not
    // match a stale row whose id is 0. chase_fire::active_cockpit keeps its
    // older walk without this guard; noted, not changed here.
    const bool result=gen!=0&&active_handle(cockpit,handle)&&handle!=0;
    SetLastError(error);return result;
}
void report(std::uint64_t frame) {
    if(!installed())return;
    detail::Window<Event> out;std::uint64_t over=0,threads=0,failed=0,skipped=0;
    detail::HandlerTiming durations[site_count]{};
    {Guard guard;out=window;window={};over=state.lifetime_overflow;threads=state.thread_overflow;failed=read_failures;skipped=suppressed;
        for(unsigned i=0;i<site_count;++i){durations[i]=handler_timing[i];handler_timing[i]={};}}
    // Split conversion avoids the i386 uint64 -> double x87 CRT sequence.
    const auto as_double=[](std::uint64_t v){return double(std::uint32_t(v>>32))*4294967296.0+double(std::uint32_t(v));};
    const double micros=frequency?1e6/as_double(frequency):0;
    if(timing)for(unsigned i=0;i<site_count;++i){const auto& c=durations[i];
        log("chase_transition_timing frame=%llu kind=%u calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=handler_after_admission_guards includes=thread_lookup_lock_wait_reads_lock_release excludes=stub_cpu_boundary_first_qpc_timing_aggregation native_work=excluded",
            frame,i,c.calls,c.samples,c.invalid,as_double(c.ticks)*micros,as_double(c.max_ticks)*micros);}
    if(restore_installed()){
        detail::RestoreState r;detail::HandlerTiming rt[restore_site_count]{};
        {Guard guard;r=restore;for(unsigned i=0;i<restore_site_count;++i){rt[i]=restore_timing[i];restore_timing[i]={};}}
        log("chase_view_restore_state frame=%llu armed=%u pending=%u arms=%llu arm_refusals=%llu arm_refusal_reasons=%llu,%llu,%llu,%llu,%llu,%llu,%llu transfers=%llu consumed=%llu writes_failed=%llu seam_calls=%llu eh_bumps=%llu epoch=%lu last_refusal=%u cancels=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu refusals=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
            frame,unsigned(r.armed),unsigned(r.pending),r.arms,r.arm_refusals,r.arm_refusal_reasons[0],r.arm_refusal_reasons[1],r.arm_refusal_reasons[2],r.arm_refusal_reasons[3],r.arm_refusal_reasons[4],r.arm_refusal_reasons[5],r.arm_refusal_reasons[6],r.transfers,r.consumed,r.writes_failed,r.seam_calls,restore_eh_bumps.load(std::memory_order_relaxed),
            static_cast<unsigned long>(restore_epoch.load(std::memory_order_relaxed)),r.last_refusal,
            r.cancels[0],r.cancels[1],r.cancels[2],r.cancels[3],r.cancels[4],r.cancels[5],r.cancels[6],r.cancels[7],r.cancels[8],r.cancels[9],r.cancels[10],r.cancels[11],r.cancels[12],r.cancels[13],r.cancels[14],
            r.refusals[0],r.refusals[1],r.refusals[2],r.refusals[3],r.refusals[4],r.refusals[5],r.refusals[6],r.refusals[7],r.refusals[8],r.refusals[9],r.refusals[10],r.refusals[11],r.refusals[12],r.refusals[13],r.refusals[14],r.refusals[15],r.refusals[16],r.refusals[17],r.refusals[18],r.refusals[19],r.refusals[20],r.refusals[21],r.refusals[22]);
        if(timing)for(unsigned i=0;i<restore_site_count;++i)if(rt[i].calls)
            log("chase_view_restore_timing frame=%llu kind=%u calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=handler_after_prefilter native_work=excluded",
                frame,i,rt[i].calls,rt[i].samples,rt[i].invalid,as_double(rt[i].ticks)*micros,as_double(rt[i].max_ticks)*micros);
    }
    if(!diagnostics_active())return;
    log("chase_transition_window frame=%llu first=%u last=%u dropped=%llu lifetime_overflow=%llu thread_overflow=%llu origin_refusals_total=%llu suppressed_total=%llu",frame,out.first_used,out.last_used,out.dropped,over,threads,failed,skipped);
    auto print=[](const Event& e){
        log("chase_transition_event event=%llu qpc=%llu generation=%llu update=%llu thread=%lu kind=%lu cockpit=0x%08lx caller=0x%08lx requested=%lu valid=%lu active_handle=%lu snapshot_valid=%lu mode=%lu connect=%lu ship=0x%08lx view=0x%08lx camera=0x%08lx sector=0x%08lx target=0x%08lx boom=%ld,%ld,%ld origin_valid=%lu origin_flags=%lu task=0x%08lx next_pc=0x%08lx context=0x%08lx context_word0=0x%08lx context_return_count=%u context_returns=%08lx:%08lx,%08lx:%08lx,%08lx:%08lx,%08lx:%08lx",
            e.sequence,e.qpc,e.generation,e.update,e.thread,e.kind,e.cockpit,e.caller,e.requested,e.valid,e.handle,
            e.before.valid,e.before.mode,e.before.connect,e.before.ship,e.before.view,e.before.camera,e.before.sector,e.before.target,
            e.before.boom[0],e.before.boom[1],e.before.boom[2],e.origin.valid,e.origin.flags,e.origin.task,e.origin.pc,e.origin.context,e.origin.context_word0,e.origin.count,
            e.origin.contexts[0],e.origin.returns[0],e.origin.contexts[1],e.origin.returns[1],e.origin.contexts[2],e.origin.returns[2],e.origin.contexts[3],e.origin.returns[3]);
        log("chase_transition_detail event=%llu geometry_valid=%lu angles_a8=%ld,%ld,%ld offset_160=%ld,%ld,%ld view_lock_120=%lu context_returns_extra=%08lx:%08lx,%08lx:%08lx identity_valid=%lu identity_refused=%lu vm=0x%08lx native_id=0x%08lx native_script=0x%08lx player_script=0x%08lx controller_script=0x%08lx monitor_script=0x%08lx script_mode=%lu monitor_ref=0x%08lx warp_phase=%lu killed=%lu cell_tags=%lu,%lu,%lu,%lu,%lu,%lu script_classes=%08lx,%08lx,%08lx,%08lx",
            e.sequence,e.geometry.valid,e.geometry.angles[0],e.geometry.angles[1],e.geometry.angles[2],
            e.geometry.offset[0],e.geometry.offset[1],e.geometry.offset[2],e.geometry.lock,
            e.origin.contexts[4],e.origin.returns[4],e.origin.contexts[5],e.origin.returns[5],
            e.identity.valid,e.identity.refused,e.identity.vm,e.identity.native_id,e.identity.native_script,
            e.identity.player,e.identity.controller,e.identity.monitor,e.identity.mode,e.identity.ref,e.identity.warp,e.identity.killed,
            e.identity.tags[0],e.identity.tags[1],e.identity.tags[2],e.identity.tags[3],e.identity.tags[4],e.identity.tags[5],
            e.identity.class_ids[0],e.identity.class_ids[1],e.identity.class_ids[2],e.identity.class_ids[3]);
    };
    for(unsigned i=0;i<out.first_used;++i)print(out.first[i]);
    const unsigned start=out.last_used==32?out.next:0;
    for(unsigned i=0;i<out.last_used;++i)print(out.last[(start+i)%32]);
}
void shutdown() {
    restore_enabled.store(false,std::memory_order_release);
    diagnostic.store(false,std::memory_order_release);enabled.store(false,std::memory_order_release);
    const bool view_restore=restore_sites_restore();
    const bool diag=restore_group(mandatory,site_count),base=restore_group(0,mandatory);
    {Guard guard;for(auto& t:state.threads)t={};for(auto& l:state.lives)l={};restore={};restore_filter_publish();}
    log("chase_transition_shutdown mandatory_restored=%u diagnostic_restored=%u view_restore_restored=%u",unsigned(base),unsigned(diag),unsigned(view_restore));
}
}
