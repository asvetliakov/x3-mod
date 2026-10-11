#include "chase_transition.h"
#include "config.h"
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

static_assert(sizeof(void*) == 4, "Verified x86 game ABI only");
namespace x3m::chase_transition {
namespace {
// All spans are whole instructions, plain-copy relocations. The constructor
// has one successful exit; the updater has two. See the focused site probe.
// clang-format off
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
// clang-format on
constexpr unsigned mandatory = 6, site_count = 9;
// X3M_CHASE_VIEW_RESTORE=1 only (docs/architecture/chase-view-restore-engine-state.md,
// section 4): the injection site at the input routine's entry (the main
// loop's input phase, outside the registry walk and every cockpit update) plus
// three program-independent VM cancel boundaries. All plain whole-instruction
// prologue copies (verify_chase_restore_sites.py proves the spans and that no
// .text branch targets their interiors).
// clang-format off
constexpr engine_patch::SiteSpec restore_specs[] = {
    {"chase_restore_inject",0x00410100,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0},
    {"chase_restore_vm_clear",0x0049ea80,{0x83,0xec,0x08,0x55,0x8b,0x6c,0x24,0x10},8,0,0},
    {"chase_restore_vm_load",0x004a0880,{0x6a,0xff,0x68,0xc8,0x00,0x53,0x00},7,0,0},
    {"chase_restore_eh_adapter",0x0052f298,{0xb8,0xf4,0xe5,0x56,0x00},5,0,0}
};
// clang-format on
constexpr unsigned restore_site_count = 4, restore_inject_kind = 0, restore_eh_kind = 3;
engine_patch::Site sites[site_count], restore_sites[restore_site_count];
std::atomic<bool> enabled{false}, diagnostic{false}, restore_enabled{false};
bool initialized = false, timing = false;
std::uint64_t frequency = 0;
detail::HandlerTiming handler_timing[site_count]{};
SRWLOCK lock = SRWLOCK_INIT;
detail::State state;
struct Guard {
    Guard() { AcquireSRWLockExclusive(&lock); }
    ~Guard() { ReleaseSRWLockExclusive(&lock); }
};
using detail::Origin;
struct Snapshot {
    std::uint32_t valid = 0, mode = 0, connect = 0, ship = 0, view = 0, camera = 0, sector = 0, target = 0;
    std::int32_t boom[3]{};
};
struct Event {
    std::uint64_t sequence = 0, qpc = 0, generation = 0, update = 0;
    std::uint32_t thread = 0, kind = 0, cockpit = 0, caller = 0, requested = 0, handle = 0, valid = 0;
    Snapshot before;
    Origin origin;
    struct Geometry {
        std::uint32_t valid = 0, lock = 0;
        std::int32_t angles[3]{}, offset[3]{};
    } geometry;
    detail::Identity identity;
};
struct Seen {
    std::uintptr_t cockpit = 0;
    std::uint64_t generation = 0;
    Event event;
    bool used = false;
};
Seen seen[4][detail::lifetime_capacity]{}, unknown_updates[detail::thread_capacity]{};
detail::Window<Event> window;
std::uint64_t sequence = 0, read_failures = 0, suppressed = 0;

bool bytes(std::uintptr_t base, unsigned offset, void* out, unsigned n) {
    if (!base || base > UINT32_MAX || offset > UINT32_MAX - base) return false;
    const auto at = base + offset;
    return n && n <= UINT32_MAX - at && engine_memory::read(at, out, n);
}
template <class T> bool field(std::uintptr_t base, unsigned offset, T& out) {
    return bytes(base, offset, &out, sizeof out);
}
std::uint64_t now() {
    LARGE_INTEGER q{};
    return QueryPerformanceCounter(&q) && q.QuadPart > 0 ? std::uint64_t(q.QuadPart) : 0;
}
void snapshot(std::uintptr_t c, Snapshot& s) {
    if (field(c, 0x150, s.mode)) s.valid |= 1;
    if (field(c, 0x1c0, s.connect)) s.valid |= 2;
    if (field(c, 0xc, s.ship)) s.valid |= 4;
    if (field(c, 0x10, s.view)) s.valid |= 8;
    if (field(c, 0x58, s.camera)) s.valid |= 16;
    if (field(c, 0x1fc, s.sector)) s.valid |= 32;
    if (field(c, 0x1e0, s.target)) s.valid |= 64;
    if (field(c, 0x130, s.boom)) s.valid |= 128;
}
// Process roots; the CPU fixture aliases them to fixture-owned memory.
std::uint32_t native_registry_root = 0x60850c, cockpit_registry_root = 0x608504;
bool active_handle(std::uintptr_t cockpit, std::uint32_t& handle) {
    std::uint32_t registry = 0, table = 0, bucket[2]{}, link = 0;
    if (!field(cockpit_registry_root, 0, registry) || !field(registry, 0, table) || !field(registry, 0x10, handle) ||
        !field(table, 0, bucket))
        return false;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !field(bucket[0], 4 * ((n - 1) & handle), link)) return false;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link, 0, row)) return false;
        if (row[1] == handle) return row[2] == cockpit;
        link = row[0];
    }
    return false;
}
// Runtime CODE length has not been established. Instead of inventing one,
// enforce overflow-free offsets and the same VirtualAlloc allocation as CODE
// base, then use the ordinary checked reader. This is conservative at split
// allocations and does not establish that arbitrary readable bytes are code.
bool code_address(std::uint32_t code, std::uint32_t offset, unsigned n, std::uintptr_t& at) {
    if (!code || offset > UINT32_MAX - code || !n || n > UINT32_MAX - (code + offset)) return false;
    at = code + offset;
    MEMORY_BASIC_INFORMATION a{}, b{};
    return VirtualQuery(reinterpret_cast<void*>(code), &a, sizeof a) == sizeof a &&
           VirtualQuery(reinterpret_cast<void*>(at + n - 1), &b, sizeof b) == sizeof b && a.State == MEM_COMMIT &&
           b.State == MEM_COMMIT && a.AllocationBase == b.AllocationBase;
}
bool same(const Event& a, const Event& b) {
    return a.kind == b.kind && a.requested == b.requested && a.caller == b.caller && a.valid == b.valid &&
           a.handle == b.handle && !std::memcmp(&a.before, &b.before, sizeof a.before) &&
           !std::memcmp(&a.origin, &b.origin, sizeof a.origin);
}
void record(unsigned kind, std::uintptr_t cockpit, std::uint32_t thread, std::uint32_t caller = 0,
            std::uint32_t requested = 0, std::uint32_t ebp = 0) {
    if (!diagnostic.load(std::memory_order_relaxed)) return;
    Event e{};
    e.kind = kind;
    e.cockpit = std::uint32_t(cockpit);
    e.thread = thread;
    e.caller = caller;
    e.requested = requested;
    auto* life = state.find(cockpit);
    e.generation = life ? life->generation : 0;
    e.update = state.current(cockpit, thread).serial;
    // Constructor entry and partial/unknown lifetimes must not read object fields.
    if (life && life->complete) {
        snapshot(cockpit, e.before);
        if (active_handle(cockpit, e.handle))
            e.valid |= 1;
        else
            e.handle = 0;
    }
    if (kind == 6) detail::provenance(ebp, e.origin, bytes, code_address);
    if (kind == 2) detail::destructor_provenance(caller, ebp, e.origin, bytes, code_address);
    if (kind == 3 && !life) {
        Seen* slot = nullptr;
        for (auto& row : unknown_updates)
            if (row.used && row.event.thread == thread) {
                slot = &row;
                break;
            }
        if (!slot)
            for (auto& row : unknown_updates)
                if (!row.used) {
                    slot = &row;
                    break;
                }
        if (!slot) {
            ++suppressed;
            return;
        }
        if (slot->used && slot->cockpit == cockpit && same(slot->event, e)) {
            ++suppressed;
            return;
        }
        *slot = {cockpit, 0, e, true};
    }
    if ((kind >= 3) && life) {
        const auto slot = unsigned(life - state.lives);
        const unsigned category = kind >= 6 ? kind - 5 : 0;
        auto& prior = seen[category][slot];
        if (prior.used && prior.cockpit == cockpit && prior.generation == e.generation && same(prior.event, e)) {
            ++suppressed;
            return;
        }
        prior = {cockpit, e.generation, e, true};
    }
    // Supplement only an admitted event. Animated angles and identity walks
    // neither change suppression nor add work on every updater/draw.
    if (life && life->complete) {
        if (field(cockpit, 0xa8, e.geometry.angles)) e.geometry.valid |= 1;
        if (field(cockpit, 0x160, e.geometry.offset)) e.geometry.valid |= 2;
        if (field(cockpit, 0x120, e.geometry.lock)) e.geometry.valid |= 4;
    }
    detail::IdentityReader<decltype(&bytes)> identity_reader{&bytes};
    identity_reader.capture((e.before.valid & 4) ? e.before.ship : 0, (e.origin.valid & 7) == 7 ? e.origin.context : 0,
                            e.identity);
    if (e.origin.flags & 1) ++read_failures;
    e.sequence = ++sequence;
    e.qpc = now();
    window.push(e);
}
// ---- one-use rear-chase restore ticket (X3M_CHASE_VIEW_RESTORE=1) ----
// Lock-protected state; the epoch is lock-free so the EH adapter can cancel
// without acquiring anything. The injection prefilter word is read racily by
// the site-I stub (nonzero only while a ticket is due); the handler
// re-validates everything under the lock from fresh reads.
detail::RestoreState restore;
std::atomic<std::uint32_t> restore_epoch{1};
std::atomic<std::uint32_t> restore_inject_filter{0};
std::atomic<std::uint64_t> restore_eh_bumps{0};
detail::HandlerTiming restore_timing[restore_site_count]{}, restore_frame_timing{};
std::uint64_t restore_report_frame = 0; // the last reported frame, for the outcome rows
// Engine roots the CPU fixture aliases to fixture-owned memory. The call
// target is the engine's by-name script call wrapper (cdecl, caller pops 0x1c):
// (object, 0, lookup key, name, 1, argc, arg...). The key resolves through
// 0x0049f1e0 (negative: the dynamic object by ~id, as the interpreter's own
// DYNCALL does), the method by a content strcmp over the object's class chain
// (0x004b0740); AL 0 when object or method is missing.
std::uint32_t game_root = detail::restore_game_root;
std::uintptr_t script_call_address = detail::restore_script_call;
const char select_mode_name[] = "SelectMode"; // mod-owned; the EXE has no such literal
using ScriptCall = std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t, std::uint32_t, const char*, std::uint32_t,
                                           std::uint32_t, std::uint32_t);
void restore_filter_publish() {
    restore_inject_filter.store(restore.wants_injection() ? 1u : 0u, std::memory_order_release);
}
// Engine walks the ticket asks for only on the frames that matter (checked
// reads only; nothing is retained).
struct EngineProbe {
    std::uintptr_t cockpit = 0;
    bool active_control(std::uint32_t& handle) {
        return active_handle(cockpit, handle) && handle != 0;
    }
    // Registry membership of the ship by its native id (the resolver SA_GetEventObject 0x004607f4 uses),
    // deferred-delete flag clear, then the engine's ship-script id +0x94.
    bool ship_live(std::uint32_t ref, std::uint32_t& ship_id) {
        detail::IdentityReader<decltype(&bytes)> r{&bytes};
        r.registry_root = native_registry_root;
        std::uint32_t native_id = 0, registry = 0, table = 0, live = 0, flags = 0;
        if (!ref || (ref & 3) || !field(ref, 8, native_id) || !field(native_registry_root, 0, registry) ||
            !r.pointer(registry) || !field(registry, 0x14, table) || !r.hash(table, native_id, live) || live != ref)
            return false;
        if (!field(ref, 0x40, flags) || (flags & detail::restore_ship_delete_flag)) return false;
        return field(ref, 0x94, ship_id);
    }
    bool controller(std::uint32_t& id) {
        std::uint32_t game = 0;
        return field(game_root, 0, game) && game && !(game & 3) && field(game, 0x418, id);
    }
};
void restore_emit_sample(const detail::RestoreArmSample& s) {
    log("chase_view_restore_arm_refused reason=%u cockpit=0x%08lx generation=%llu mode=%lu connect=%lu ship=0x%08lx view=0x%08lx handle=%lu ship_live=%u ship_id=0x%08lx armed=%u",
        s.reason, static_cast<unsigned long>(s.cockpit), s.generation, static_cast<unsigned long>(s.mode),
        static_cast<unsigned long>(s.connect), static_cast<unsigned long>(s.ship), static_cast<unsigned long>(s.view),
        static_cast<unsigned long>(s.handle), unsigned(s.ship_live), static_cast<unsigned long>(s.ship_id),
        unsigned(s.armed));
}
void restore_emit_outcome(const detail::RestoreOutcome& o) {
    log("chase_view_restore result=%s cancel=%s attempts=%u monitor=0x%08lx monitor_class=0x%lx mode_seen=%lu generation_from=%llu generation_to=%llu ship_id_from=0x%08lx ship_id_to=0x%08lx sector_from=0x%08lx sector_to=0x%08lx wait_updates=%lu frame=%llu",
        detail::restore_result_name(o.result), detail::restore_cancel_name(o.cancel), o.attempts,
        static_cast<unsigned long>(o.monitor), static_cast<unsigned long>(o.monitor_class),
        static_cast<unsigned long>(o.mode_seen), o.generation_from, o.generation_to,
        static_cast<unsigned long>(o.ship_id_from), static_cast<unsigned long>(o.ship_id_to),
        static_cast<unsigned long>(o.sector_from), static_cast<unsigned long>(o.sector_to),
        static_cast<unsigned long>(o.wait_updates), restore_report_frame);
}
struct RestoreRows {
    detail::RestoreArmSample sample;
    detail::RestoreOutcome outcome;
    void take() {
        sample = restore.take_arm_sample();
        outcome = restore.take_outcome();
    }
    void emit() const { // never under the Guard: log() takes capture's mutex, which present() holds around report()
        if (sample.wanted) restore_emit_sample(sample);
        if (outcome.wanted) restore_emit_outcome(outcome);
    }
};
// Fresh frame of a cockpit for the injection re-check: cockpit fields through the checked reader, lifetime
// facts from the witness table (unseen or partial lifetimes read as generation 0, which the core refuses).
void restore_read_frame(std::uintptr_t cockpit, detail::RestoreFrame& f) {
    f = {};
    f.cockpit = std::uint32_t(cockpit);
    if (const auto* life = state.find(cockpit)) {
        f.generation = life->complete ? life->generation : 0;
        f.constructor_caller = life->constructor_caller;
    }
    f.readable = cockpit && !(cockpit & 3) && field(cockpit, 0x150, f.mode) && field(cockpit, 0x1c0, f.connect) &&
                 field(cockpit, 0x1a0, f.flags) && field(cockpit, 0xc, f.ref) && field(cockpit, 0x10, f.view) &&
                 field(cockpit, 0x1fc, f.sector);
}
// Update-begin (kind 3): the expiry clock, every cockpit update.
void restore_on_update() {
    if (!restore_enabled.load(std::memory_order_relaxed) || !restore.ticket_open()) return;
    restore.on_update();
    restore_filter_publish();
}
void restore_on_destroy(std::uintptr_t cockpit, std::uint32_t caller) {
    if (!restore_enabled.load(std::memory_order_relaxed) || !restore.ticket_open()) return;
    const auto* life = state.find(cockpit);
    restore.on_destroy(std::uint32_t(cockpit), life ? life->generation : 0, caller);
    restore_filter_publish();
}
void restore_on_complete(std::uintptr_t cockpit) {
    if (!restore_enabled.load(std::memory_order_relaxed) || !restore.ticket_open()) return;
    const auto* life = state.find(cockpit);
    restore.on_complete(life ? life->constructor_caller : 0);
    restore_filter_publish();
}
// Kind 6 (0x0042e742: EAX cockpit, ECX mode, EBP the dispatcher frame): the
// script object making the store is read from the engine's VM task frame,
// the same chain provenance() validates: [ebp+4] == 0x4a3909 (the
// interpreter's native-call site), [ebp+0xc] task, [task+0x3c] current
// context, [context] object id, [context+8] class descriptor, [desc] class id.
// Only a dynamic object id (negative: the interpreter's own DYNCALL resolves it through the object table)
// is accepted; a non-negative value would make the by-name call index the static class table.
void restore_on_mode_store(std::uint32_t* regs, std::uint32_t thread) {
    if (!restore_enabled.load(std::memory_order_relaxed) || restore.phase < detail::phase_pending) return;
    const std::uintptr_t cockpit = regs[7];
    const std::uint32_t requested = regs[6], ebp = regs[2];
    const auto* life = state.find(cockpit);
    std::uint32_t return_pc = 0, task = 0, context = 0, id = 0, desc = 0, class_id = 0;
    if (!(field(ebp, 4, return_pc) && return_pc == 0x4a3909 && field(ebp, 0xc, task) && task && !(task & 3) &&
          field(task, 0x3c, context) && context && !(context & 3) && field(context, 0, id) &&
          std::int32_t(id) < 0 && field(context, 8, desc) && desc && !(desc & 3) && field(desc, 0, class_id))) {
        id = 0;
        class_id = 0;
    }
    restore.on_mode_store(std::uint32_t(cockpit), life && life->complete ? life->generation : 0, requested, thread,
                          life ? life->constructor_caller : 0, id, class_id);
    restore_filter_publish();
}
// Deserialization kind7 (the existing mode-load observer, 0x419e06): a cancel, no new site.
void restore_on_load() {
    if (!restore_enabled.load(std::memory_order_relaxed)) return;
    restore.on_load();
    restore_filter_publish();
}
void restore_on_vm() {
    restore_epoch.fetch_add(1, std::memory_order_acq_rel);
    restore.on_vm();
    restore_filter_publish();
}
// The engine's own by-name script call on the captured monitor object:
// `SelectMode(258)`, the method the script's view keys end in. The call runs
// under the engine's incoming x87/MXCSR/LastError image (the callee is engine
// code) and the handler's clean state is re-established afterwards; the
// caller's CpuState brings the engine image back at the stub boundary.
__attribute__((noinline)) bool restore_engine_select_mode(const detail::RestoreCall& c, x3m::CpuState& cpu) {
    const auto fn = reinterpret_cast<ScriptCall>(script_call_address);
    cpu.restore();
    const std::uint32_t r = fn(c.monitor, 0, c.monitor, select_mode_name, 1, 1, detail::restore_rear_mode) & 0xff;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    return r != 0;
}
// Site I (0x00410100 entry, once per main-loop iteration): consume a due
// ticket. No lock, RAII object or borrowed engine pointer is held across the
// engine call (a script error unwinds through this frame; the EH adapter's
// epoch bump is the cancel).
__attribute__((noinline)) void inject_handle(std::uint32_t* regs, x3m::CpuState& cpu) {
    if (!restore_enabled.load(std::memory_order_acquire)) return;
    const auto start = timing ? now() : 0;
    const auto thread = GetCurrentThreadId();
    const auto esp = regs[3] + 4;
    detail::RestoreCall call;
    RestoreRows rows;
    {
        Guard guard;
        if (restore.wants_injection()) {
            detail::RestoreFrame fresh;
            std::uint32_t controlled = 0, game = 0, pause_bits = 0;
            restore_read_frame(restore.bound_cockpit, fresh);
            field(esp, 4, controlled);
            if (field(game_root, 0, game) && game) field(game, 0x4a0, pause_bits);
            EngineProbe probe{restore.bound_cockpit};
            call = restore.on_inject_entry(thread, fresh, controlled, controlled ? pause_bits : 1, probe,
                                           restore_epoch.load(std::memory_order_acquire));
        }
        restore_filter_publish();
        rows.take();
    }
    if (call.wanted) {
        const bool ok = restore_engine_select_mode(call, cpu);
        Guard guard;
        restore.on_call_result(ok);
        restore_filter_publish();
        rows.take();
    }
    rows.emit();
    if (timing) {
        const auto end = now();
        Guard guard;
        restore_timing[restore_inject_kind].add(start, end);
    }
}
void restore_handle(unsigned kind, std::uint32_t* regs) {
    (void)regs;
    if (kind >= restore_site_count || kind == restore_inject_kind || !restore_enabled.load(std::memory_order_acquire))
        return;
    if (kind == restore_eh_kind) {
        // Lock-free: native unwinding may pass through this adapter while
        // another observer holds its lock on the same or another thread.
        restore_epoch.fetch_add(1, std::memory_order_acq_rel);
        restore_eh_bumps.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto start = timing ? now() : 0;
    RestoreRows rows;
    {
        Guard guard;
        restore_on_vm();
        rows.take();
    }
    rows.emit();
    if (timing) {
        const auto end = now();
        Guard guard;
        restore_timing[kind].add(start, end);
    }
}
void handle(unsigned kind, std::uint32_t* regs) {
    if (kind >= site_count || !enabled.load(std::memory_order_acquire) ||
        (kind >= mandatory && !diagnostic.load(std::memory_order_relaxed) &&
         !restore_enabled.load(std::memory_order_relaxed)))
        return;
    const auto start = timing ? now() : 0;
    const auto thread = GetCurrentThreadId();
    const auto esp = regs[3] + 4; // PUSHFD precedes PUSHAD's saved ESP.
    std::uint32_t cockpit = 0, caller = 0;
    RestoreRows rows;
    {
        Guard guard;
        switch (kind) {
        case 0:
            // Reentry retires this thread even if the constructor argument is unreadable.
            chase_lead::native_timing_invalidate(0, thread);
            if (field(esp, 4, cockpit)) {
                field(esp, 0, caller);
                chase_lead::native_timing_invalidate(cockpit, thread);
                state.construct(cockpit, thread, caller);
                record(0, cockpit, thread, caller);
            }
            break;
        case 1:
            cockpit = regs[7];
            if (state.complete(cockpit, thread)) {
                record(1, cockpit, thread);
                restore_on_complete(cockpit);
            }
            break;
        case 2:
            chase_lead::native_timing_invalidate(0, thread);
            if (field(esp, 4, cockpit)) {
                field(esp, 0, caller);
                record(2, cockpit, thread, caller, 0, regs[2]);
                restore_on_destroy(cockpit, caller);
                chase_lead::native_timing_invalidate(cockpit, thread);
                state.destroy(cockpit);
            }
            break;
        case 3:
            // Even a failed argument read must revoke this thread's previous update.
            chase_lead::native_timing_invalidate(0, thread);
            field(esp, 4, cockpit);
            state.begin(cockpit, esp, thread);
            record(3, cockpit, thread);
            restore_on_update();
            break;
        case 4:
        case 5:
            chase_lead::native_timing_invalidate(0, thread);
            state.end(regs[2] + 4, thread);
            break;
        case 6:
            record(6, regs[7], thread, 0x42e742, regs[6], regs[2]);
            restore_on_mode_store(regs, thread);
            break;
        case 7:
            record(7, regs[2], thread, 0x419e06, regs[5]);
            restore_on_load();
            break;
        case 8:
            field(esp, 0, caller);
            record(8, regs[1], thread, caller, regs[7]);
            break;
        }
        rows.take();
    }
    rows.emit(); // never under the Guard
    // Include the handler lock acquisition/release and checked reads. The
    // second lock only aggregates this sample and lies outside its interval.
    if (timing) {
        const auto end = now();
        Guard guard;
        handler_timing[kind].add(start, end);
    }
}
}
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_transition_enter(unsigned kind,
                                                                                            std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_transition::handle(kind, regs);
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_restore_enter(unsigned kind,
                                                                                         std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_transition::restore_handle(kind, regs);
}
// Site I: a plain CpuState (no destructor) so a script error unwinding from
// the engine call through this frame runs nothing of ours; the same capture
// and restore inventory as the other callbacks.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_inject_enter(unsigned kind,
                                                                                        std::uint32_t* regs) {
    (void)kind;
    x3m::CpuState cpu;
    cpu.capture();
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_transition::inject_handle(regs, cpu);
    cpu.restore();
}
namespace x3m::chase_transition {
namespace {
// Register-saving stub. With `filter` a one-word prefilter runs under the
// saved flags before any XMM/x87 capture: `cmp dword [filter], 0; je skip`,
// so an idle ticket costs the site one memory compare per call.
void* emit_stub(unsigned kind, const void* callback, void*** next_out, const std::atomic<std::uint32_t>* filter) {
    engine_patch::Emitter e(256);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    e.byte(0x9c); // pushfd
    const unsigned char* skip_at = nullptr;
    if (filter) {
        constexpr unsigned prefilter = 7 + 6, full_path = 116;
        skip_at = static_cast<const unsigned char*>(e.here()) + prefilter + full_path;
        e.byte(0x83);
        e.byte(0x3d);
        e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(filter)));
        e.byte(0);
        e.byte(0x0f);
        e.byte(0x84);
        e.rel32(skip_at); // cmp dword [filter],0; je skip
        if (e.here() != skip_at - full_path) return nullptr;
    }
    e.byte(0x60);
    e.byte(0xfc); // pushad; provide the C ABI's clear DF (DF itself is in the saved flags)
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x80);
    for (unsigned i = 0; i < 8; ++i) {
        e.byte(0x0f);
        e.byte(0x11);
        e.byte(static_cast<unsigned char>(0x44 | (i << 3)));
        e.byte(0x24);
        e.byte(static_cast<unsigned char>(i * 16));
    }
    e.byte(0x8d);
    e.byte(0x84);
    e.byte(0x24);
    e.dword(0x80);
    e.byte(0x50);
    e.byte(0x68);
    e.dword(kind);
    e.byte(0xe8);
    e.rel32(callback);
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(8);
    for (unsigned i = 0; i < 8; ++i) {
        e.byte(0x0f);
        e.byte(0x10);
        e.byte(static_cast<unsigned char>(0x44 | (i << 3)));
        e.byte(0x24);
        e.byte(static_cast<unsigned char>(i * 16));
    }
    e.byte(0x81);
    e.byte(0xc4);
    e.dword(0x80);
    e.byte(0x61);
    if (filter && e.here() != skip_at) return nullptr;
    e.byte(0x9d); // popfd
    const auto next = (reinterpret_cast<std::uintptr_t>(e.here()) + 6 + 3) & ~std::uintptr_t(3);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(next));
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < next) e.byte(0xcc);
    *next_out = reinterpret_cast<void**>(next);
    e.dword(0);
    return e.finish() ? start : nullptr;
}
void* emit(unsigned kind, void*** next_out) {
    return emit_stub(kind, reinterpret_cast<const void*>(&x3m_chase_transition_enter), next_out, nullptr);
}
void* emit_restore(unsigned kind, void*** next_out) {
    if (kind == restore_inject_kind)
        return emit_stub(kind, reinterpret_cast<const void*>(&x3m_chase_inject_enter), next_out,
                         &restore_inject_filter);
    return emit_stub(kind, reinterpret_cast<const void*>(&x3m_chase_restore_enter), next_out, nullptr);
}
bool restore_group(unsigned first, unsigned end) {
    bool okay = true;
    for (unsigned i = end; i-- > first;)
        if (sites[i].patched_in && !engine_patch::restore(sites[i])) okay = false;
    return okay;
}
bool install_group(unsigned first, unsigned end) {
    for (unsigned i = first; i < end; ++i) {
        if (!engine_patch::claim(sites[i], specs[i])) {
            restore_group(first, end);
            return false;
        }
        void** next = nullptr;
        void* stub = emit(i, &next);
        if (!stub || !next || !engine_patch::store_pointer(next, *sites[i].entry) ||
            !engine_patch::push_front(sites[i], stub)) {
            restore_group(first, end);
            return false;
        }
    }
    return true;
}
// Partial-install rollback: a refused/failed site restores every earlier one.
bool restore_sites_restore() {
    bool okay = true;
    for (unsigned i = restore_site_count; i-- > 0;)
        if (restore_sites[i].patched_in && !engine_patch::restore(restore_sites[i])) okay = false;
    return okay;
}
bool restore_sites_install(const engine_patch::SiteSpec* table) {
    for (unsigned i = 0; i < restore_site_count; ++i) {
        if (!engine_patch::claim(restore_sites[i], table[i])) {
            restore_sites_restore();
            return false;
        }
        void** next = nullptr;
        void* stub = emit_restore(i, &next);
        if (!stub || !next || !engine_patch::store_pointer(next, *restore_sites[i].entry) ||
            !engine_patch::push_front(restore_sites[i], stub)) {
            restore_sites_restore();
            return false;
        }
    }
    return true;
}
bool restore_wanted() {
    wchar_t setting[8]{};
    return x3m::config::get(L"X3M_CHASE_VIEW_RESTORE", setting, 8) == 1 && setting[0] == L'1';
}
// X3M_CHASE_VIEW_RESTORE_RETRIES: 0..5 (default 3); anything else refuses the install (fail closed).
bool restore_retries(unsigned& out) {
    wchar_t setting[8]{};
    const DWORD n = x3m::config::get(L"X3M_CHASE_VIEW_RESTORE_RETRIES", setting, 8);
    if (n == 0) {
        out = detail::restore_default_retries;
        return true;
    }
    if (n != 1 || setting[0] < L'0' || setting[0] > L'0' + detail::restore_retry_cap) return false;
    out = unsigned(setting[0] - L'0');
    return true;
}
}
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return installed();
    }
    initialized = true;
    const bool wanted = chase_camera::wanted() && chase_camera::installed();
    const bool okay = wanted && object_trace::executable_verified() && engine_patch::install_window_open() &&
                      install_group(0, mandatory);
    LARGE_INTEGER f{};
    timing = okay && telemetry::enabled() && QueryPerformanceFrequency(&f) && f.QuadPart > 0;
    frequency = timing ? std::uint64_t(f.QuadPart) : 0;
    enabled.store(okay, std::memory_order_release);
    // The three mode/load/connect observers are installed with telemetry (the
    // diagnostic window) or with the restore ticket, which needs the mode
    // store (monitor capture, call result) and the deserializer (cancel).
    const bool restore_requested = restore_wanted();
    unsigned retries = detail::restore_default_retries;
    const bool retries_ok = restore_retries(retries);
    const bool observers = okay && (telemetry::enabled() || restore_requested) && install_group(mandatory, site_count);
    const bool diag = observers && telemetry::enabled();
    diagnostic.store(diag, std::memory_order_release);
    log("chase_transition installed=%u diagnostics=%u observers=%u mandatory_sites=6 diagnostic_sites=3 mode_writes=0 lifetime_capacity=64 thread_capacity=8 timing=%u qpc_frequency=%llu identity_version=1 origin_pairs=6 identity_scope=admitted_events",
        unsigned(okay), unsigned(diag), unsigned(observers), unsigned(timing), frequency);
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i].patched_in || wanted)
            log("chase_transition_site index=%u site=0x%08lx patched=%u status=%s", i,
                static_cast<unsigned long>(specs[i].address), unsigned(sites[i].patched_in), sites[i].status);
    // With the option unset nothing below is claimed. The engine path needs
    // the observers (kind 6/7); the call target and roots are the EXE's own
    // (object_trace::executable_verified() bound the image above).
    const bool restore_okay = restore_requested && okay && observers && retries_ok &&
                              engine_patch::install_window_open() && restore_sites_install(restore_specs);
    {
        Guard guard;
        restore = {};
        restore.retries = retries;
        restore_filter_publish();
    }
    restore_enabled.store(restore_okay, std::memory_order_release);
    if (restore_requested || restore_okay) {
        log("chase_view_restore installed=%u requested=%u path=engine sites=1 cancel_sites=%u inject_site=0x%08lx call=0x%08lx method=SelectMode mode=%u observers=%u retries=%u retries_valid=%u expiry_updates=%u due_frames=%u retry_spacing=%u",
            unsigned(restore_okay), unsigned(restore_requested), restore_site_count - 1,
            static_cast<unsigned long>(detail::restore_inject_site),
            static_cast<unsigned long>(detail::restore_script_call), detail::restore_rear_mode, unsigned(observers),
            retries, unsigned(retries_ok), detail::restore_pending_update_expiry, detail::restore_due_frames,
            detail::restore_retry_spacing);
        for (unsigned i = 0; i < restore_site_count; ++i)
            log("chase_view_restore_site index=%u site=0x%08lx patched=%u status=%s", i,
                static_cast<unsigned long>(restore_specs[i].address), unsigned(restore_sites[i].patched_in),
                restore_sites[i].status);
    }
    SetLastError(error);
    return okay;
}
bool restore_installed() {
    return restore_enabled.load(std::memory_order_acquire);
}
bool installed() {
    return enabled.load(std::memory_order_acquire);
}
bool diagnostics_active() {
    return diagnostic.load(std::memory_order_acquire);
}
std::uint64_t generation(std::uintptr_t cockpit) noexcept {
    if (!installed()) return 0;
    const DWORD error = GetLastError();
    std::uint64_t result;
    {
        Guard guard;
        result = state.generation(cockpit);
    }
    SetLastError(error);
    return result;
}
Update current_update(std::uintptr_t cockpit) noexcept {
    if (!installed()) return {};
    const DWORD error = GetLastError();
    Update result;
    {
        Guard guard;
        result = state.current(cockpit, GetCurrentThreadId());
    }
    SetLastError(error);
    return result;
}
bool active_control_cockpit(std::uintptr_t cockpit) noexcept {
    if (!installed() || !cockpit || (cockpit & 3)) return false;
    const DWORD error = GetLastError();
    std::uint64_t gen = 0;
    std::uint32_t handle = 0;
    // The generation is sampled under the lock; the bounded registry walk then
    // runs unlocked (checked engine reads only). A destroy between the two is
    // excluded only because both run on the game thread, inside the cockpit
    // update whose lifetime witness produced the generation.
    {
        Guard guard;
        gen = state.generation(cockpit);
    }
    // handle != 0: a registry with no active control (slot +0x10 zero) must not
    // match a stale row whose id is 0. chase_fire::active_cockpit keeps its
    // older walk without this guard; noted, not changed here.
    const bool result = gen != 0 && active_handle(cockpit, handle) && handle != 0;
    SetLastError(error);
    return result;
}
// Chase hook visit (0x00420e06, inside the cockpit update on the game thread).
// One atomic load when the option is off; otherwise the ticket's arm/due
// logic under the lock, with the registry walks only on frames that matter.
// Unseen or partial lifetimes read as generation 0 and never arm.
void restore_observe_frame(const AppliedFrame& frame) noexcept {
    if (!restore_enabled.load(std::memory_order_acquire)) return;
    const DWORD error = GetLastError();
    const auto start = timing ? now() : 0;
    detail::RestoreFrame f;
    f.cockpit = std::uint32_t(frame.cockpit);
    f.mode = frame.mode;
    f.connect = frame.connect;
    f.flags = frame.flags;
    f.ref = frame.ref;
    f.view = frame.view;
    f.sector = frame.sector;
    f.readable = frame.cockpit != 0 && !(frame.cockpit & 3);
    RestoreRows rows;
    {
        Guard guard;
        if (const auto* life = state.find(frame.cockpit)) {
            f.generation = life->complete ? life->generation : 0;
            f.constructor_caller = life->constructor_caller;
        }
        if (f.generation) {
            EngineProbe probe{frame.cockpit};
            restore.on_frame(f, probe, restore_epoch.load(std::memory_order_acquire));
            restore_filter_publish();
        }
        rows.take();
    }
    rows.emit();
    if (timing) {
        const auto end = now();
        Guard guard;
        restore_frame_timing.add(start, end);
    }
    SetLastError(error);
}
void report(std::uint64_t frame) {
    if (!installed()) return;
    detail::Window<Event> out;
    std::uint64_t over = 0, threads = 0, failed = 0, skipped = 0;
    detail::HandlerTiming durations[site_count]{};
    {
        Guard guard;
        out = window;
        window = {};
        over = state.lifetime_overflow;
        threads = state.thread_overflow;
        failed = read_failures;
        skipped = suppressed;
        for (unsigned i = 0; i < site_count; ++i) {
            durations[i] = handler_timing[i];
            handler_timing[i] = {};
        }
    }
    // Split conversion avoids the i386 uint64 -> double x87 CRT sequence.
    const auto as_double = [](std::uint64_t v) {
        return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
    };
    const double micros = frequency ? 1e6 / as_double(frequency) : 0;
    if (timing)
        for (unsigned i = 0; i < site_count; ++i) {
            const auto& c = durations[i];
            log("chase_transition_timing frame=%llu kind=%u calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=handler_after_admission_guards includes=thread_lookup_lock_wait_reads_lock_release excludes=stub_cpu_boundary_first_qpc_timing_aggregation native_work=excluded",
                frame, i, c.calls, c.samples, c.invalid, as_double(c.ticks) * micros, as_double(c.max_ticks) * micros);
        }
    if (restore_installed()) {
        detail::RestoreState r;
        detail::HandlerTiming rt[restore_site_count]{}, ft{};
        {
            Guard guard;
            restore_report_frame = frame;
            r = restore;
            for (unsigned i = 0; i < restore_site_count; ++i) {
                rt[i] = restore_timing[i];
                restore_timing[i] = {};
            }
            ft = restore_frame_timing;
            restore_frame_timing = {};
        }
        if (timing)
            log("chase_view_restore_state frame=%llu phase=%u arms=%llu arm_refusals=%llu arm_refusal_reasons=%llu,%llu,%llu,%llu,%llu,%llu,%llu pendings=%llu dues=%llu calls=%llu monitors_captured=%llu injections_skipped=%llu rearm_frames=%llu eh_bumps=%llu epoch=%lu results=%llu,%llu,%llu,%llu,%llu,%llu cancels=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu monitor=0x%08lx attempts=%u",
                frame, r.phase, r.arms, r.arm_refusals, r.arm_refusal_reasons[0], r.arm_refusal_reasons[1],
                r.arm_refusal_reasons[2], r.arm_refusal_reasons[3], r.arm_refusal_reasons[4],
                r.arm_refusal_reasons[5], r.arm_refusal_reasons[6], r.pendings, r.dues, r.calls, r.monitors_captured,
                r.injections_skipped, r.rearm_frames, restore_eh_bumps.load(std::memory_order_relaxed),
                static_cast<unsigned long>(restore_epoch.load(std::memory_order_relaxed)), r.results[0],
                r.results[1], r.results[2], r.results[3], r.results[4], r.results[5], r.cancels[0], r.cancels[1],
                r.cancels[2], r.cancels[3], r.cancels[4], r.cancels[5], r.cancels[6], r.cancels[7], r.cancels[8],
                r.cancels[9], r.cancels[10], r.cancels[11], r.cancels[12], r.cancels[13],
                static_cast<unsigned long>(r.monitor),
                r.attempts);
        if (timing)
            for (unsigned i = 0; i < restore_site_count; ++i)
                if (rt[i].calls)
                    log("chase_view_restore_timing frame=%llu kind=%u calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=handler_after_prefilter native_work=%s",
                        frame, i, rt[i].calls, rt[i].samples, rt[i].invalid, as_double(rt[i].ticks) * micros,
                        as_double(rt[i].max_ticks) * micros,
                        i == restore_inject_kind ? "included_on_call_iterations" : "excluded");
        if (timing && ft.calls)
            log("chase_view_restore_timing frame=%llu kind=frame calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=chase_hook_observer native_work=excluded",
                frame, ft.calls, ft.samples, ft.invalid, as_double(ft.ticks) * micros, as_double(ft.max_ticks) * micros);
    }
    if (!diagnostics_active()) return;
    log("chase_transition_window frame=%llu first=%u last=%u dropped=%llu lifetime_overflow=%llu thread_overflow=%llu origin_refusals_total=%llu suppressed_total=%llu",
        frame, out.first_used, out.last_used, out.dropped, over, threads, failed, skipped);
    auto print = [](const Event& e) {
        log("chase_transition_event event=%llu qpc=%llu generation=%llu update=%llu thread=%lu kind=%lu cockpit=0x%08lx caller=0x%08lx requested=%lu valid=%lu active_handle=%lu snapshot_valid=%lu mode=%lu connect=%lu ship=0x%08lx view=0x%08lx camera=0x%08lx sector=0x%08lx target=0x%08lx boom=%ld,%ld,%ld origin_valid=%lu origin_flags=%lu task=0x%08lx next_pc=0x%08lx context=0x%08lx context_word0=0x%08lx context_return_count=%u context_returns=%08lx:%08lx,%08lx:%08lx,%08lx:%08lx,%08lx:%08lx",
            e.sequence, e.qpc, e.generation, e.update, e.thread, e.kind, e.cockpit, e.caller, e.requested, e.valid,
            e.handle, e.before.valid, e.before.mode, e.before.connect, e.before.ship, e.before.view, e.before.camera,
            e.before.sector, e.before.target, e.before.boom[0], e.before.boom[1], e.before.boom[2], e.origin.valid,
            e.origin.flags, e.origin.task, e.origin.pc, e.origin.context, e.origin.context_word0, e.origin.count,
            e.origin.contexts[0], e.origin.returns[0], e.origin.contexts[1], e.origin.returns[1], e.origin.contexts[2],
            e.origin.returns[2], e.origin.contexts[3], e.origin.returns[3]);
        log("chase_transition_detail event=%llu geometry_valid=%lu angles_a8=%ld,%ld,%ld offset_160=%ld,%ld,%ld view_lock_120=%lu context_returns_extra=%08lx:%08lx,%08lx:%08lx identity_valid=%lu identity_refused=%lu vm=0x%08lx native_id=0x%08lx native_script=0x%08lx player_script=0x%08lx controller_script=0x%08lx monitor_script=0x%08lx script_mode=%lu monitor_ref=0x%08lx warp_phase=%lu killed=%lu cell_tags=%lu,%lu,%lu,%lu,%lu,%lu script_classes=%08lx,%08lx,%08lx,%08lx",
            e.sequence, e.geometry.valid, e.geometry.angles[0], e.geometry.angles[1], e.geometry.angles[2],
            e.geometry.offset[0], e.geometry.offset[1], e.geometry.offset[2], e.geometry.lock, e.origin.contexts[4],
            e.origin.returns[4], e.origin.contexts[5], e.origin.returns[5], e.identity.valid, e.identity.refused,
            e.identity.vm, e.identity.native_id, e.identity.native_script, e.identity.player, e.identity.controller,
            e.identity.monitor, e.identity.mode, e.identity.ref, e.identity.warp, e.identity.killed, e.identity.tags[0],
            e.identity.tags[1], e.identity.tags[2], e.identity.tags[3], e.identity.tags[4], e.identity.tags[5],
            e.identity.class_ids[0], e.identity.class_ids[1], e.identity.class_ids[2], e.identity.class_ids[3]);
    };
    for (unsigned i = 0; i < out.first_used; ++i) print(out.first[i]);
    const unsigned start = out.last_used == 32 ? out.next : 0;
    for (unsigned i = 0; i < out.last_used; ++i) print(out.last[(start + i) % 32]);
}
void shutdown() {
    restore_enabled.store(false, std::memory_order_release);
    diagnostic.store(false, std::memory_order_release);
    enabled.store(false, std::memory_order_release);
    const bool view_restore = restore_sites_restore();
    const bool diag = restore_group(mandatory, site_count), base = restore_group(0, mandatory);
    {
        Guard guard;
        for (auto& t : state.threads) t = {};
        for (auto& l : state.lives) l = {};
        restore = {};
        restore_filter_publish();
    }
    log("chase_transition_shutdown mandatory_restored=%u diagnostic_restored=%u view_restore_restored=%u",
        unsigned(base), unsigned(diag), unsigned(view_restore));
}
}
