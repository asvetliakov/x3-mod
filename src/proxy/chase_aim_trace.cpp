#include "chase_aim_trace.h"
#include "chase_camera.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "telemetry.h"
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "Verified x86 game ABI only");
namespace x3m::chase_aim_trace {
namespace {
// Derived, instruction-validated spans: chase-mouse-fire.md. No relative
// instructions; EBX is the original argument frame, EBP the realigned locals.
constexpr unsigned site_count = 4;
constexpr engine_patch::SiteSpec specs[] = {
    {"chase_fire_gate", 0x00445a15, {0x8b,0x43,0x14,0x8b,0xf8}, 5,0,0},
    {"chase_fire_ray", 0x00445b70, {0x89,0x45,0xe4,0x8b,0x45,0xe4}, 6,0,0},
    {"chase_fire_final", 0x0044605a, {0x8b,0x53,0x08,0x8b,0x4a,0x70}, 6,0,0},
    {"chase_cursor_write", 0x004074de, {0x89,0x0d,0xe8,0x7c,0x60,0x00}, 6,0,0}
};
constexpr unsigned capacity = 8, thread_capacity = 4;
engine_patch::Site sites[site_count];
std::atomic<bool> enabled{false};
std::atomic<std::uint32_t> player{0};
bool initialized = false;
const char* install_status = "disabled";
SRWLOCK lock = SRWLOCK_INIT;

// Every structure is bounded static/stack storage. Validity is explicit: no
// unreadable field is interpreted as a real zero value in the admission test.
struct Pose {
    std::uint32_t valid = 0, ship_id = 0;
    std::int32_t camera_pos[3]{}, camera_basis[12]{}, relative[12]{};
    std::int32_t ship_pos[3]{}, ship_basis[12]{};
    std::uint32_t fov = 0, viewport[4]{}, plane[2]{}, default_plane[2]{}, screen[2]{};
};
struct Context {
    std::uint64_t sequence = 0, qpc = 0, handler_frame = 0;
    std::uint32_t cockpit = 0, ship = 0, camera = 0, mode = 0;
    bool applied = false;
    Pose pose;
};
struct CursorUpdate {
    std::uint64_t sequence = 0, qpc = 0, pose_sequence = 0;
    std::uint32_t mode = 0, thread = 0;
    std::int32_t active = 0, x = 0, y = 0;
};
struct Event {
    std::uint64_t serial = 0, entry_qpc = 0, ray_qpc = 0, final_qpc = 0;
    std::uint32_t thread = 0, ebx = 0, ebp = 0, args[5]{};
    Context context;
    CursorUpdate last_cursor_write;
    Pose fire_pose;
    std::uint32_t valid = 0, cockpit = 0, camera = 0, view_object = 0, mode = 0, picked_target = 0;
    std::uint64_t pose_age_ticks = 0;
    std::int32_t cursor_active = 0, cursor_x = 0, cursor_y = 0;
    std::int32_t aim_gun = -1, gun_count = -1, mapped_gun = -1, cone[3]{};
    std::uint32_t gate = 0, finals = 0;
    bool admitted = false, cone_clamped = false;
    std::int32_t depth = 0, ray_length = 0, ray[3]{}, group_origin[3]{};
    std::int32_t first_direction[3]{}, direction[3]{}, muzzle[3]{}, endpoint[3]{};
    std::int32_t cursor_marker = 0;
};
struct Pending { std::uint32_t thread = 0, ebx = 0, ebp = 0; std::uint64_t serial = 0; int slot = -1; };
struct Counts {
    std::uint64_t entries = 0, rays = 0, finals = 0, dropped = 0, orphans = 0;
    std::uint64_t read_failures = 0, cone_clamped = 0, thread_overflow = 0, omitted_followups = 0;
    std::uint64_t gates[10]{};
    std::uint64_t writer_updates = 0, writer_on = 0, writer_changes = 0, writer_moves = 0;
    std::uint64_t handler_calls = 0, ticks = 0, max_ticks = 0;
};
Context context;
CursorUpdate last_cursor_write, writer_first[3], writer_last[3];
std::uint64_t writer_sequence=0;
Event events[capacity];
Pending pending[thread_capacity];
Counts counts;
unsigned used = 0;
std::uint64_t event_serial = 0;
LARGE_INTEGER frequency{};

bool read(std::uintptr_t base, unsigned offset, void* out, unsigned size) {
    if (!base || (base & 3) || offset > UINT32_MAX - base) return false;
    const auto address = base + offset;
    if (size > UINT32_MAX - address) return false;
    return engine_memory::read(address, out, size);
}
template<class T> bool field(std::uintptr_t base, unsigned offset, T& out) {
    return read(base, offset, &out, sizeof out);
}
std::uint64_t now() {
    LARGE_INTEGER stamp{};
    return QueryPerformanceCounter(&stamp) && stamp.QuadPart > 0 ? std::uint64_t(stamp.QuadPart) : 0;
}
void pose(std::uint32_t cockpit, std::uint32_t ship, std::uint32_t camera, Pose& p) {
    p = {};
    if (field(camera,0x30,p.camera_pos)) p.valid |= 1;
    if (field(camera,0x40,p.camera_basis)) p.valid |= 2;
    if (field(cockpit,0xf0,p.relative)) p.valid |= 4;
    std::uint32_t node = 0;
    if (field(ship,0x70,node)) {
        if (field(node,0x30,p.ship_pos)) p.valid |= 8;
        if (field(node,0x40,p.ship_basis)) p.valid |= 16;
    }
    if (field(ship,8,p.ship_id)) p.valid |= 32;
    if (field(camera,0x298,p.fov)) p.valid |= 64;
    if (field(camera,0x288,p.viewport)) p.valid |= 128;
    if (field(camera,0x300,p.plane)) p.valid |= 256;
    std::uint32_t defaults=0, screen=0;
    std::int16_t dimensions[2]{};
    if(field(0x00606f38,0,defaults)) {
        if(field(defaults,0x28,p.default_plane))p.valid|=512;
        if(field(defaults,0,screen) && field(screen,4,dimensions) && dimensions[0]>0 && dimensions[1]>0) {
            p.screen[0]=std::uint32_t(dimensions[0]);p.screen[1]=std::uint32_t(dimensions[1]);p.valid|=1024;
        }
    }
}
// Exact registry layout from 0x41cd20. Diagnostic caps reject corruption or
// unexpected scale; they never call the engine resolver or affect admission.
bool active_cockpit(std::uint32_t& result) {
    std::uint32_t registry=0, table=0, handle=0, bucket[2]{}, link=0;
    if (!field(0x00608504,0,registry) || !field(registry,0,table) ||
        !field(registry,0x10,handle) || !field(table,0,bucket)) return false;
    const std::uint32_t n=bucket[1];
    if (!n || n>65536 || (n & (n-1)) || !field(bucket[0],4*((n-1)&handle),link)) return false;
    for (unsigned i=0; link && i<32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link,0,row)) return false;
        if (row[1]==handle) { result=row[2]; return result && !(result&3); }
        link=row[0];
    }
    return false;
}
bool gun_mapping(Event& e) {
    std::uint32_t identity=0, types=0;
    if (!field(e.args[0],0x48,identity) || !field(0x00606fd4,0,types)) return false;
    if ((identity & 0xffff)!=7) { e.gun_count=0; e.mapped_gun=0; return true; }
    const auto subtype=std::int16_t(identity>>16);
    if (subtype<0) return false;
    const std::uint32_t offset=std::uint32_t(subtype)*0xdb8;
    if (!field(types,offset+0xd4,e.gun_count)) return false;
    if (e.aim_gun<0 || e.aim_gun>=e.gun_count) return true;
    if (e.aim_gun>4096) return false;
    return field(types,offset+0xdc+std::uint32_t(e.aim_gun)*0x18,e.mapped_gun);
}
Pending* thread_pending(std::uint32_t thread, bool create) {
    Pending* empty=nullptr;
    for (auto& p:pending) {
        if (p.thread==thread) return &p;
        if (!p.thread && !empty) empty=&p;
    }
    if (create && empty) { empty->thread=thread; return empty; }
    return nullptr;
}
// Gate numbers: 0 expected admitted,1 no fire bit,2 no cursor bit,3 inactive
// cursor,4 unreadable registry,5 wrong ship,6 bad aim index,7 bad mapping,
// 8 unavailable field,9 stale/unknown camera context. These are observations,
// not decisions injected into the game.
void admission(Event& e) {
    std::int32_t cursor[3]{};
    if (field(0x00607ce8,0,cursor)) {
        e.cursor_active=cursor[0];e.cursor_x=cursor[1];e.cursor_y=cursor[2];e.valid|=1;
    }
    if (field(0x00587b88,0,e.cone)) e.valid|=2;
    if (active_cockpit(e.cockpit)) e.valid|=4;
    if ((e.valid&4) && field(e.cockpit,0x10,e.view_object) &&
        field(e.cockpit,0x1d8,e.aim_gun) && field(e.cockpit,0x150,e.mode) && field(e.cockpit,0x58,e.camera)) e.valid|=8;
    if ((e.valid&8) && gun_mapping(e)) e.valid|=16;
    pose(e.cockpit,e.args[0],e.camera,e.fire_pose);
    const bool age_known=e.entry_qpc && e.context.qpc && e.entry_qpc>=e.context.qpc;
    e.pose_age_ticks=age_known?e.entry_qpc-e.context.qpc:UINT64_MAX;
    // A two-second witness limit labels stale diagnostics; it never gates
    // native firing or rejects the legitimate previous displayed frame.
    const bool age_valid=age_known && frequency.QuadPart>0 &&
        e.pose_age_ticks<=std::uint64_t(frequency.QuadPart)*2;
    if (!(e.args[3]&2)) e.gate=1;
    else if (!(e.args[3]&0x20)) e.gate=2;
    else if (!(e.valid&1)) e.gate=8;
    else if (!e.cursor_active) e.gate=3;
    else if (!(e.valid&4)) e.gate=4;
    else if (!(e.valid&8)) e.gate=8;
    else if (e.view_object!=e.args[0]) e.gate=5;
    else if (!(e.valid&16)) e.gate=8;
    else if (e.aim_gun<0 || e.aim_gun>=e.gun_count) e.gate=6;
    else if (e.mapped_gun!=std::int32_t(e.args[2])) e.gate=7;
    else if (!(e.context.pose.valid&32) || !(e.fire_pose.valid&32) ||
             e.context.pose.ship_id!=e.fire_pose.ship_id || e.context.cockpit!=e.cockpit ||
             e.context.ship!=e.args[0] || !e.context.camera || e.context.camera!=e.camera || !age_valid) e.gate=9;
}
void observe(unsigned phase, std::uint32_t* regs) {
    if (!enabled.load(std::memory_order_acquire) || phase>=site_count) return;
    if (phase==3) {
        // This command owns one global cursor state, so observe every write,
        // including before a player context exists. Register inputs avoid the
        // command record's intentionally unaligned +1/+6/+b/+10 arguments.
        const auto stamp=now();
        AcquireSRWLockExclusive(&lock);
        CursorUpdate w{};w.sequence=++writer_sequence;w.qpc=stamp;
        w.pose_sequence=context.sequence;w.mode=context.mode;w.thread=GetCurrentThreadId();
        w.active=std::int32_t(regs[6]);w.x=std::int32_t(regs[5]);w.y=std::int32_t(regs[7]);
        ++counts.writer_updates;if(w.active)++counts.writer_on;
        if(last_cursor_write.sequence && w.active!=last_cursor_write.active)++counts.writer_changes;
        if(last_cursor_write.sequence && (w.x!=last_cursor_write.x || w.y!=last_cursor_write.y))++counts.writer_moves;
        const unsigned bucket=!context.ship?0:context.mode==1?1:2;
        if(!writer_first[bucket].sequence)writer_first[bucket]=w;
        writer_last[bucket]=w;last_cursor_write=w;
        const auto end=now();const auto ticks=end>stamp?end-stamp:0;
        ++counts.handler_calls;counts.ticks+=ticks;if(ticks>counts.max_ticks)counts.max_ticks=ticks;
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    // Early player-address filter avoids registry walks, locks and pose reads
    // for AI weapons. It is not used to authorize any game mutation.
    const std::uint32_t ebx=regs[4], ebp=regs[2];
    std::uint32_t ship=0;
    if (!field(ebx,8,ship) || !ship || ship!=player.load(std::memory_order_relaxed)) return;
    const auto stamp=now();
    const DWORD thread=GetCurrentThreadId();
    AcquireSRWLockExclusive(&lock);
    const auto begin=stamp;
    Pending* p=thread_pending(thread,phase==0);
    if (phase==0) {
        ++counts.entries;
        // Invalidate before reading/sampling the new call. Reused EBP/EBX or
        // a full sample window must never append to an earlier call's record.
        if (p) { p->ebx=ebx;p->ebp=ebp;p->serial=++event_serial;p->slot=-1; }
        else { ++event_serial; ++counts.thread_overflow; }
        Event e{};
        e.serial=event_serial;e.thread=thread;e.ebx=ebx;e.ebp=ebp;e.entry_qpc=stamp;e.context=context;e.last_cursor_write=last_cursor_write;
        if (!field(ebx,8,e.args)) { ++counts.read_failures; }
        else {
            admission(e);
            ++counts.gates[e.gate];
            if (p && used<capacity) { p->slot=int(used);events[used++]=e; }
            else ++counts.dropped;
        }
    } else {
        if (phase==1) ++counts.rays; else ++counts.finals;
        if (p && p->ebx==ebx && p->ebp==ebp && p->slot<0) {
            ++counts.omitted_followups;
        } else if (!p || p->ebx!=ebx || p->ebp!=ebp ||
                   unsigned(p->slot)>=used || events[p->slot].serial!=p->serial) {
            ++counts.orphans;
        } else {
        Event& e=events[p->slot];
        if (phase==1) {
            e.admitted=true;e.ray_qpc=stamp;e.ray_length=std::int32_t(regs[7]);
            if (ebp>=0x130 && field(ebp-0x30,0,e.ray) && field(ebp-0x14,0,e.depth) &&
                field(ebp-0x130,0,e.group_origin)) {
                e.valid|=32;
                if(field(ebx,0xc,e.picked_target))e.valid|=256;
                if (e.valid&2) {
                    const std::int64_t product=std::int64_t(e.ray_length)*e.cone[0]+0x8000;
                    e.cone_clamped=std::int64_t(e.ray[2])<(product>>16);
                    if (e.cone_clamped) ++counts.cone_clamped;
                }
            } else ++counts.read_failures;
        } else {
            e.final_qpc=stamp;
            if (ebp>=0x100 && field(ebp-0x50,0,e.direction) && field(ebp-0xf0,0,e.muzzle) &&
                field(ebp-0x74,0,e.cursor_marker)) {
                e.valid|=64;
                if (!e.finals) std::memcpy(e.first_direction,e.direction,sizeof e.direction);
                ++e.finals;
                if (e.admitted && field(ebp-0x100,0,e.endpoint)) e.valid|=128;
            } else ++counts.read_failures;
        }
    }
    }
    const auto end=now();const auto ticks=end>begin?end-begin:0;
    ++counts.handler_calls;counts.ticks+=ticks;if (ticks>counts.max_ticks) counts.max_ticks=ticks;
    ReleaseSRWLockExclusive(&lock);
}
}
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_chase_aim_enter(std::uint32_t* regs, unsigned phase) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr=0x1f80;
    asm volatile("ldmxcsr %0" :: "m"(mxcsr) : "memory");
    x3m::chase_aim_trace::observe(phase,regs);
}
namespace x3m::chase_aim_trace {
namespace {
void* emit(unsigned phase,void*** next_out) {
    engine_patch::Emitter e(176);
    if (!e.ok()) return nullptr;
    void* start=e.here();
    e.byte(0x9c);e.byte(0x60);e.byte(0x81);e.byte(0xec);e.dword(0x80);
    for(unsigned i=0;i<8;++i) { e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16)); }
    e.byte(0x8d);e.byte(0x84);e.byte(0x24);e.dword(0x80);
    e.byte(0x68);e.dword(phase);e.byte(0x50);
    e.byte(0xe8);e.rel32(reinterpret_cast<const void*>(&x3m_chase_aim_enter));
    e.byte(0x83);e.byte(0xc4);e.byte(8);
    for(unsigned i=0;i<8;++i) { e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16)); }
    e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x61);e.byte(0x9d);
    const auto next=(reinterpret_cast<std::uintptr_t>(e.here())+6+3)&~std::uintptr_t(3);
    e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));
    while(e.ok() && reinterpret_cast<std::uintptr_t>(e.here())<next)e.byte(0xcc);
    *next_out=reinterpret_cast<void**>(next);e.dword(0);
    return e.finish()?start:nullptr;
}
void log_pose(std::uint64_t frame,std::uint64_t serial,const char* which,const Pose& p) {
    log("chase_aim_pose frame=%llu serial=%llu which=%s valid=0x%lx ship_id=0x%lx camera_pos=%ld,%ld,%ld ship_pos=%ld,%ld,%ld fov=%lu viewport=%lu,%lu,%lu,%lu plane=%lu,%lu default_plane=%lu,%lu screen=%lu,%lu",
        frame,serial,which,static_cast<unsigned long>(p.valid),static_cast<unsigned long>(p.ship_id),
        static_cast<long>(p.camera_pos[0]),static_cast<long>(p.camera_pos[1]),static_cast<long>(p.camera_pos[2]),
        static_cast<long>(p.ship_pos[0]),static_cast<long>(p.ship_pos[1]),static_cast<long>(p.ship_pos[2]),static_cast<unsigned long>(p.fov),
        static_cast<unsigned long>(p.viewport[0]),static_cast<unsigned long>(p.viewport[1]),static_cast<unsigned long>(p.viewport[2]),static_cast<unsigned long>(p.viewport[3]),static_cast<unsigned long>(p.plane[0]),static_cast<unsigned long>(p.plane[1]),static_cast<unsigned long>(p.default_plane[0]),static_cast<unsigned long>(p.default_plane[1]),static_cast<unsigned long>(p.screen[0]),static_cast<unsigned long>(p.screen[1]));
    const std::int32_t* matrices[]={p.camera_basis,p.relative,p.ship_basis};
    const char* names[]={"camera","relative","ship"};
    for(unsigned m=0;m<3;++m) { const auto* a=matrices[m];
        log("chase_aim_basis frame=%llu serial=%llu which=%s matrix=%s rows=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",frame,serial,which,names[m],
            static_cast<long>(a[0]),static_cast<long>(a[1]),static_cast<long>(a[2]),static_cast<long>(a[4]),static_cast<long>(a[5]),static_cast<long>(a[6]),static_cast<long>(a[8]),static_cast<long>(a[9]),static_cast<long>(a[10]));
    }
}
}
bool initialize() {
    const DWORD error=GetLastError();
    if(initialized) { SetLastError(error);return enabled.load(); }
    initialized=true;
    if(!telemetry::enabled() || !chase_camera::installed() || !chase_camera::wanted()) { SetLastError(error);return false; }
    bool okay=object_trace::executable_verified() && engine_patch::install_window_open();
    install_status=okay?"installing":"prerequisite_failed";
    if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) { okay=false;install_status="no_qpc_frequency"; }
    const char* failure=nullptr;
    for(unsigned i=0;i<site_count && okay;++i) {
        okay=engine_patch::claim(sites[i],specs[i]);
        if(okay) {
            void** next=nullptr;void* stub=emit(i,&next);
            if (!stub || !next) { okay=false;failure="stub_failed"; }
            else if (!engine_patch::store_pointer(next,*sites[i].entry)) { okay=false;failure="chain_pointer_failed"; }
            else if (!engine_patch::push_front(sites[i],stub)) { okay=false;failure="chain_failed"; }
        }
        if(!okay)install_status=failure?failure:sites[i].status;
    }
    if(!okay) {
        bool restored=true;
        for(auto& site:sites)if(site.patched_in)restored=engine_patch::restore(site)&&restored;
        if(!restored)install_status="rollback_failed_diagnostic_disabled";
    } else install_status="active";
    enabled.store(okay,std::memory_order_release);
    log("chase_aim_trace installed=%u status=%s sites=%u samples_per_report=%u scope=read_only_player_fire lifetime=process",
        unsigned(okay),install_status,unsigned(sites[0].patched_in)+unsigned(sites[1].patched_in)+unsigned(sites[2].patched_in)+unsigned(sites[3].patched_in),capacity);
    SetLastError(error);return okay;
}
void invalidate_camera(std::uintptr_t cockpit) {
    if(!enabled.load(std::memory_order_acquire))return;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    // An unreadable monitor is not evidence that the active player's context
    // changed. Only invalidate an address we previously accepted as current.
    if(context.cockpit==cockpit) {
        const auto sequence=context.sequence+1;
        context={};context.sequence=sequence;context.qpc=now();
        player.store(0,std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
void camera_context(std::uintptr_t cockpit,std::uintptr_t ship,std::uintptr_t camera,std::uint32_t mode,bool applied,std::uint64_t handler_frame) {
    if(!enabled.load(std::memory_order_acquire))return;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    ++context.sequence;context.qpc=now();context.cockpit=std::uint32_t(cockpit);context.ship=std::uint32_t(ship);
    context.camera=std::uint32_t(camera);context.mode=mode;context.applied=applied;context.handler_frame=handler_frame;
    pose(context.cockpit,context.ship,context.camera,context.pose);
    player.store((context.pose.valid&32)?context.ship:0,std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
}
void report(std::uint64_t frame) {
    if(!enabled.load(std::memory_order_acquire))return;
    Counts c;Event sample[capacity];CursorUpdate first[3],last[3];unsigned n;
    AcquireSRWLockExclusive(&lock);
    c=counts;counts={};n=used;
    std::memcpy(first,writer_first,sizeof first);std::memcpy(last,writer_last,sizeof last);
    for(unsigned i=0;i<3;++i){writer_first[i]={};writer_last[i]={};}
    std::memcpy(sample,events,n*sizeof(Event));used=0;
    for(auto& p:pending)p.slot=-1;
    ReleaseSRWLockExclusive(&lock);
    log("chase_aim_window frame=%llu entries=%llu rays=%llu finals=%llu samples=%u dropped=%llu orphans=%llu read_failures=%llu sampled_cone_clamped=%llu thread_overflow=%llu omitted_followups=%llu gates=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu timing_scope=player_fire_and_cursor_writer_after_first_qpc excludes=stub_cpu_boundary_first_qpc_and_fire_player_filter handler_calls=%llu handler_ticks=%llu max_ticks=%llu frequency=%llu",
        frame,c.entries,c.rays,c.finals,n,c.dropped,c.orphans,c.read_failures,c.cone_clamped,c.thread_overflow,c.omitted_followups,
        c.gates[0],c.gates[1],c.gates[2],c.gates[3],c.gates[4],c.gates[5],c.gates[6],c.gates[7],c.gates[8],c.gates[9],c.handler_calls,c.ticks,c.max_ticks,std::uint64_t(frequency.QuadPart));
    log("chase_cursor_window frame=%llu updates=%llu active=%llu inactive=%llu active_changes=%llu moves=%llu",frame,c.writer_updates,c.writer_on,c.writer_updates-c.writer_on,c.writer_changes,c.writer_moves);
    for(unsigned b=0;b<3;++b) for(unsigned k=0;k<2;++k) {
        const auto& w=k?last[b]:first[b];if(!w.sequence)continue;
        log("chase_cursor_update frame=%llu bucket=%u which=%s seq=%llu qpc=%llu pose_seq=%llu mode=%lu thread=%lu cursor=%ld,%ld,%ld",frame,b,k?"last":"first",w.sequence,w.qpc,w.pose_sequence,static_cast<unsigned long>(w.mode),static_cast<unsigned long>(w.thread),static_cast<long>(w.active),static_cast<long>(w.x),static_cast<long>(w.y));
    }
    for(unsigned i=0;i<n;++i) {
        const auto& e=sample[i];
        log("chase_aim_writer frame=%llu serial=%llu writer_seq=%llu writer_qpc=%llu writer_pose_seq=%llu writer_mode=%lu writer_thread=%lu cursor=%ld,%ld,%ld",frame,e.serial,e.last_cursor_write.sequence,e.last_cursor_write.qpc,e.last_cursor_write.pose_sequence,static_cast<unsigned long>(e.last_cursor_write.mode),static_cast<unsigned long>(e.last_cursor_write.thread),static_cast<long>(e.last_cursor_write.active),static_cast<long>(e.last_cursor_write.x),static_cast<long>(e.last_cursor_write.y));
        log("chase_aim_event frame=%llu serial=%llu thread=%lu ebx=0x%lx ebp=0x%lx ship=0x%lx target=0x%lx gun=%lu fire_flags=0x%lx mask=0x%lx valid=0x%lx gate=%lu cursor=%ld,%ld,%ld cockpit=0x%lx view_object=0x%lx mode=%lu aim_gun=%ld gun_count=%ld mapped_gun=%ld admitted=%u finals=%lu cursor_marker=%ld pose_seq=%llu pose_handler_frame=%llu pose_qpc=%llu entry_qpc=%llu ray_qpc=%llu final_qpc=%llu pose_mode=%lu pose_applied=%u pose_age_ticks=%llu pose_age_limit_ms=2000 camera=0x%lx picked_target=0x%lx",
            frame,e.serial,static_cast<unsigned long>(e.thread),static_cast<unsigned long>(e.ebx),static_cast<unsigned long>(e.ebp),static_cast<unsigned long>(e.args[0]),static_cast<unsigned long>(e.args[1]),static_cast<unsigned long>(e.args[2]),static_cast<unsigned long>(e.args[3]),static_cast<unsigned long>(e.args[4]),static_cast<unsigned long>(e.valid),static_cast<unsigned long>(e.gate),
            static_cast<long>(e.cursor_active),static_cast<long>(e.cursor_x),static_cast<long>(e.cursor_y),static_cast<unsigned long>(e.cockpit),static_cast<unsigned long>(e.view_object),static_cast<unsigned long>(e.mode),static_cast<long>(e.aim_gun),static_cast<long>(e.gun_count),static_cast<long>(e.mapped_gun),unsigned(e.admitted),static_cast<unsigned long>(e.finals),static_cast<long>(e.cursor_marker),
            e.context.sequence,e.context.handler_frame,e.context.qpc,e.entry_qpc,e.ray_qpc,e.final_qpc,static_cast<unsigned long>(e.context.mode),unsigned(e.context.applied),e.pose_age_ticks,static_cast<unsigned long>(e.camera),static_cast<unsigned long>(e.picked_target));
        log("chase_aim_ray frame=%llu serial=%llu depth=%ld length=%ld ray=%ld,%ld,%ld cone=%ld,%ld,%ld clamped=%u origin=%ld,%ld,%ld first_dir=%ld,%ld,%ld final_dir=%ld,%ld,%ld muzzle=%ld,%ld,%ld endpoint=%ld,%ld,%ld",
            frame,e.serial,static_cast<long>(e.depth),static_cast<long>(e.ray_length),static_cast<long>(e.ray[0]),static_cast<long>(e.ray[1]),static_cast<long>(e.ray[2]),static_cast<long>(e.cone[0]),static_cast<long>(e.cone[1]),static_cast<long>(e.cone[2]),unsigned(e.cone_clamped),
            static_cast<long>(e.group_origin[0]),static_cast<long>(e.group_origin[1]),static_cast<long>(e.group_origin[2]),static_cast<long>(e.first_direction[0]),static_cast<long>(e.first_direction[1]),static_cast<long>(e.first_direction[2]),static_cast<long>(e.direction[0]),static_cast<long>(e.direction[1]),static_cast<long>(e.direction[2]),static_cast<long>(e.muzzle[0]),static_cast<long>(e.muzzle[1]),static_cast<long>(e.muzzle[2]),static_cast<long>(e.endpoint[0]),static_cast<long>(e.endpoint[1]),static_cast<long>(e.endpoint[2]));
        log_pose(frame,e.serial,"camera_event",e.context.pose);log_pose(frame,e.serial,"fire_event",e.fire_pose);
    }
}
}
